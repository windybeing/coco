//
// Created by Yi Lu on 9/13/18.
//

#pragma once

#include "core/Partitioner.h"

#include "common/Percentile.h"
#include "core/Delay.h"
#include "core/Worker.h"
#include "glog/logging.h"

#include "protocol/Calvin/Calvin.h"
#include "protocol/Calvin/CalvinHelper.h"
#include "protocol/Calvin/CalvinMessage.h"

#include <chrono>
#include <thread>

namespace scar {

template <class Workload> class CalvinExecutor : public Worker {
public:
  using WorkloadType = Workload;
  using DatabaseType = typename WorkloadType::DatabaseType;
  using StorageType = typename WorkloadType::StorageType;

  using TableType = typename DatabaseType::TableType;
  using TransactionType = CalvinTransaction;
  static_assert(std::is_same<typename WorkloadType::TransactionType,
                             TransactionType>::value,
                "Transaction types do not match.");

  using ContextType = typename DatabaseType::ContextType;
  using RandomType = typename DatabaseType::RandomType;

  using ProtocolType = Calvin<DatabaseType>;

  using MessageType = CalvinMessage;
  using MessageFactoryType = CalvinMessageFactory;
  using MessageHandlerType = CalvinMessageHandler;

  CalvinExecutor(std::size_t coordinator_id, std::size_t id, DatabaseType &db,
                 const ContextType &context,
                 std::vector<std::unique_ptr<TransactionType>> &transactions,
                 std::atomic<uint32_t> &complete_transaction_num,
                 std::atomic<uint32_t> &worker_status,
                 std::atomic<uint32_t> &n_complete_workers,
                 std::atomic<uint32_t> &n_started_workers)
      : Worker(coordinator_id, id), db(db), context(context),
        transactions(transactions),
        complete_transaction_num(complete_transaction_num),
        worker_status(worker_status), n_complete_workers(n_complete_workers),
        n_started_workers(n_started_workers),
        partitioner(coordinator_id, context.coordinator_num,
                    CalvinHelper::string_to_vint(context.replica_group)),
        n_lock_manager(CalvinHelper::n_lock_manager(
            partitioner.replica_group_id, id,
            CalvinHelper::string_to_vint(context.lock_manager))),
        n_workers(context.worker_num - n_lock_manager),
        lock_manager_id(CalvinHelper::worker_id_to_lock_manager_id(
            id, n_lock_manager, n_workers)),
        protocol(db, partitioner),
        delay(std::make_unique<SameDelay>(
            coordinator_id, context.coordinator_num, context.delay_time)) {

    for (auto i = 0u; i < context.coordinator_num; i++) {
      messages.emplace_back(std::make_unique<Message>());
      init_message(messages[i].get(), i);
    }

    messageHandlers = MessageHandlerType::get_message_handlers();
    CHECK(n_workers > 0 && n_workers % n_lock_manager == 0);
  }

  ~CalvinExecutor() = default;

  void start() override {
    LOG(INFO) << "CalvinExecutor " << id << " started. ";

    for (;;) {

      ExecutorStatus status;
      do {
        status = static_cast<ExecutorStatus>(worker_status.load());

        if (status == ExecutorStatus::EXIT) {
          LOG(INFO) << "CalvinExecutor " << id << " exits. ";
          return;
        }
      } while (status != ExecutorStatus::Analysis);

      n_started_workers.fetch_add(1);
      for (auto i = id; i < transactions.size(); i += context.worker_num) {
        prepare_transaction(*transactions[i]);
        complete_transaction_num.fetch_add(1);
      }
      n_complete_workers.fetch_add(1);

      // wait to START

      while (static_cast<ExecutorStatus>(worker_status.load()) !=
             ExecutorStatus::Execute) {
        std::this_thread::yield();
      }

      n_started_workers.fetch_add(1);
      // work as lock manager
      if (id < n_lock_manager) {
        // schedule transactions
        schedule_transactions();
      } else {
        // work as executor
        run_transactions();
      }

      n_complete_workers.fetch_add(1);
    }
  }

  void onExit() override {}

  void push_message(Message *message) override { in_queue.push(message); }

  Message *pop_message() override {
    if (out_queue.empty())
      return nullptr;

    Message *message = out_queue.front();

    if (delay->delay_enabled()) {
      auto now = std::chrono::steady_clock::now();
      if (std::chrono::duration_cast<std::chrono::microseconds>(now -
                                                                message->time)
              .count() < delay->message_delay()) {
        return nullptr;
      }
    }

    bool ok = out_queue.pop();
    CHECK(ok);

    return message;
  }

  void flush_messages() {

    for (auto i = 0u; i < messages.size(); i++) {
      if (i == coordinator_id) {
        continue;
      }

      if (messages[i]->get_message_count() == 0) {
        continue;
      }

      auto message = messages[i].release();

      out_queue.push(message);
      messages[i] = std::make_unique<Message>();
      init_message(messages[i].get(), i);
    }
  }

  void init_message(Message *message, std::size_t dest_node_id) {
    message->set_source_node_id(coordinator_id);
    message->set_dest_node_id(dest_node_id);
    message->set_worker_id(id);
  }

  void prepare_transaction(TransactionType &txn) {
    setup_prepare_handlers(txn);
    // run execute to prepare read/write set
    auto result = txn.execute();
    if (result == TransactionResult::ABORT_NORETRY) {
      txn.abort_no_retry = true;
    }

    analyze_active_coordinator(txn);
  }

  void analyze_active_coordinator(TransactionType &transaction) {

    // assuming no blind write
    auto &readSet = transaction.readSet;
    auto &active_coordinators = transaction.active_coordinators;
    active_coordinators =
        std::vector<bool>(partitioner.total_coordinators(), false);

    for (auto i = 0u; i < readSet.size(); i++) {
      auto &readkey = readSet[i];
      if (readkey.get_local_index_read_bit()) {
        continue;
      }
      auto partitionID = readkey.get_partition_id();
      if (readkey.get_write_lock_bit()) {
        active_coordinators[partitioner.master_coordinator(partitionID)] = true;
      }
    }
  }

  void schedule_transactions() {

    // grant locks, once all locks are acquired, assign the transaction to
    // a worker thread in a round-robin manner.

    for (auto i = 0u; i < transactions.size(); i++) {
      // do not grant locks to abort no retry transaction
      if (!transactions[i]->abort_no_retry) {
        auto &readSet = transactions[i]->readSet;
        for (auto k = 0u; k < readSet.size(); k++) {
          auto &readKey = readSet[k];
          auto tableId = readKey.get_table_id();
          auto partitionId = readKey.get_partition_id();

          if (!partitioner.has_master_partition(partitionId)) {
            continue;
          }

          auto table = db.find_table(tableId, partitionId);
          auto key = readKey.get_key();

          if (readKey.get_local_index_read_bit()) {
            continue;
          }

          std::atomic<uint64_t> &tid = table->search_metadata(key);
          if (readKey.get_write_lock_bit()) {
            CalvinHelper::write_lock(tid);
          } else if (readKey.get_read_lock_bit()) {
            CalvinHelper::read_lock(tid);
          } else {
            CHECK(false);
          }
        }

        auto worker = get_available_worker();
        all_transaction_queues[worker]->push(transactions[i].get());
      }
    }
  }

  void run_transactions() {

    while (complete_transaction_num.load() < context.batch_size) {

      if (transaction_queue.empty()) {
        std::this_thread::yield();
        continue;
      }

      TransactionType *transaction = transaction_queue.front();
      bool ok = transaction_queue.pop();
      DCHECK(ok);

      setup_execute_handlers(*transaction);
      transaction->execution_phase = true;
      auto result = transaction->execute();
      n_network_size.fetch_add(transaction->network_size);
      if (result == TransactionResult::READY_TO_COMMIT) {
        protocol.commit(*transaction, lock_manager_id, n_lock_manager,
                        partitioner.replica_group_size);
        n_commit.fetch_add(1);
      } else if (result == TransactionResult::ABORT) {
        // non-active transactions, release lock
        protocol.abort(*transaction, lock_manager_id, n_lock_manager,
                       partitioner.replica_group_size);
        n_commit.fetch_add(1);
      } else {
        n_abort_no_retry.fetch_add(1);
      }

      complete_transaction_num.fetch_add(1);
    }
  }

  void setup_execute_handlers(TransactionType &txn) {
    txn.read_handler = [this, &txn](std::size_t table_id,
                                    std::size_t partition_id, std::size_t id,
                                    uint32_t key_offset, const void *key,
                                    void *value) {
      if (partitioner.has_master_partition(partition_id)) {
        TableType *table = this->db.find_table(table_id, partition_id);
        CalvinHelper::read(table->search(key), value, table->value_size());

        auto &active_coordinators = txn.active_coordinators;
        for (auto i = 0u; i < active_coordinators.size(); i++) {
          if (i == coordinator_id || !active_coordinators[i])
            continue;
          auto sz = MessageFactoryType::new_read_message(*messages[i], *table,
                                                         id, key_offset, value);
          txn.network_size += sz;
        }
        txn.local_read.fetch_add(-1);
      }
    };

    txn.setup_process_requests_in_execution_phase(
        lock_manager_id, n_lock_manager, partitioner.replica_group_size);
    txn.remote_request_handler = [this]() { return this->process_request(); };
    txn.message_flusher = [this]() { this->flush_messages(); };
  };

  void setup_prepare_handlers(TransactionType &txn) {
    txn.local_index_read_handler = [this](std::size_t table_id,
                                          std::size_t partition_id,
                                          const void *key, void *value) {
      TableType *table = this->db.find_table(table_id, partition_id);
      CalvinHelper::read(table->search(key), value, table->value_size());
    };
    txn.setup_process_requests_in_prepare_phase();
  };

  void set_all_transaction_queues(
      const std::vector<LockfreeQueue<TransactionType *> *> &queues) {
    all_transaction_queues = queues;
  }

  std::size_t get_available_worker() {
    // assume there are n lock managers and m workers
    // 0, 1, .. n-1 are lock managers
    // n, n + 1, .., n + m -1 are workers

    // the first lock managers assign transactions to n, .. , n + m/n - 1

    auto start_worker_id = n_lock_manager + n_workers / n_lock_manager * id;
    auto len = n_workers / n_lock_manager;
    return random.uniform_dist(start_worker_id, start_worker_id + len - 1);
  }

  std::size_t process_request() {

    std::size_t size = 0;

    while (!in_queue.empty()) {
      std::unique_ptr<Message> message(in_queue.front());
      bool ok = in_queue.pop();
      CHECK(ok);

      for (auto it = message->begin(); it != message->end(); it++) {

        MessagePiece messagePiece = *it;
        auto type = messagePiece.get_message_type();
        DCHECK(type < messageHandlers.size());
        TableType *table = db.find_table(messagePiece.get_table_id(),
                                         messagePiece.get_partition_id());
        messageHandlers[type](messagePiece,
                              *messages[message->get_source_node_id()], *table,
                              transactions);
      }

      size += message->get_message_count();
      flush_messages();
    }
    return size;
  }

public:
  LockfreeQueue<TransactionType *> transaction_queue;

private:
  DatabaseType &db;
  const ContextType &context;
  std::vector<std::unique_ptr<TransactionType>> &transactions;
  std::atomic<uint32_t> &complete_transaction_num, &worker_status;
  std::atomic<uint32_t> &n_complete_workers, &n_started_workers;
  CalvinPartitioner partitioner;
  std::size_t n_lock_manager, n_workers;
  std::size_t lock_manager_id;
  RandomType random;
  ProtocolType protocol;
  std::unique_ptr<Delay> delay;
  std::vector<std::unique_ptr<Message>> messages;
  std::vector<
      std::function<void(MessagePiece, Message &, TableType &,
                         std::vector<std::unique_ptr<TransactionType>> &)>>
      messageHandlers;
  LockfreeQueue<Message *> in_queue, out_queue;

  std::vector<LockfreeQueue<TransactionType *> *> all_transaction_queues;
};
} // namespace scar