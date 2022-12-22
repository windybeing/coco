//
// Created by Yi Lu on 9/10/18.
//

#pragma once

#include "common/Percentile.h"
#include "core/ControlMessage.h"
#include "core/Defs.h"
#include "core/Delay.h"
#include "core/Partitioner.h"
#include "core/Worker.h"
#include "glog/logging.h"

#include <chrono>

namespace coco {
namespace group_commit {

template <class Workload, class Protocol> class Executor : public Worker {
public:
  using WorkloadType = Workload;
  using ProtocolType = Protocol;
  using DatabaseType = typename WorkloadType::DatabaseType;
  using TransactionType = typename WorkloadType::TransactionType;
  using ContextType = typename DatabaseType::ContextType;
  using RandomType = typename DatabaseType::RandomType;
  using MessageType = typename ProtocolType::MessageType;
  using MessageFactoryType = typename ProtocolType::MessageFactoryType;
  using MessageHandlerType = typename ProtocolType::MessageHandlerType;

  using StorageType = typename WorkloadType::StorageType;

  Executor(std::size_t coordinator_id, std::size_t id, DatabaseType &db,
           const ContextType &context, std::atomic<uint32_t> &worker_status,
           std::atomic<uint32_t> &n_complete_workers,
           std::atomic<uint32_t> &n_started_workers)
      : Worker(coordinator_id, id), db(db), context(context),
        worker_status(worker_status), n_complete_workers(n_complete_workers),
        n_started_workers(n_started_workers),
        partitioner(PartitionerFactory::create_partitioner(
            context.partitioner, coordinator_id, context.coordinator_num)),
        random(reinterpret_cast<uint64_t>(this)),
        protocol(db, context, *partitioner),
        workload(coordinator_id, db, random, *partitioner),
        delay(std::make_unique<SameDelay>(
            coordinator_id, context.coordinator_num, context.delay_time)) {

    for (auto i = 0u; i < context.coordinator_num; i++) {
      sync_messages.emplace_back(std::make_unique<Message>());
      init_message(sync_messages[i].get(), i);

      async_messages.emplace_back(std::make_unique<Message>());
      init_message(async_messages[i].get(), i);
    }

    messageHandlers = MessageHandlerType::get_message_handlers();
    message_stats.resize(messageHandlers.size(), 0);
    message_sizes.resize(messageHandlers.size(), 0);
  }

  void start() override {

    LOG(INFO) << "Executor " << id << " starts.";

    StorageType storage;
    uint64_t last_seed = 0;

    // transaction only commit in a single group

    std::queue<std::unique_ptr<TransactionType>> q;
    std::size_t count = 0;
    auto time = std::chrono::steady_clock::now();
    Percentile<int64_t> process_request_lat;
    Percentile<int64_t> sync_message_lat;
    Percentile<int64_t> process_request2_lat, first_lat, second_lat, third_lat, total_execution_lat;
    for (;;) {

      ExecutorStatus status;
      do {
        status = static_cast<ExecutorStatus>(worker_status.load());

        if (status == ExecutorStatus::EXIT) {
          LOG(INFO) << "Executor " << id << " exits.";
          return;
        }
      } while (status != ExecutorStatus::START);

      while (!q.empty()) {
        auto &ptr = q.front();
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now() - ptr->startTime)
                           .count();
        commit_latency.add(latency);
        q.pop();
      }

      n_started_workers.fetch_add(1);

      bool retry_transaction = false;


      // uint32_t n_epoch_abort = 0;
      // uint32_t n_epoch_commit = 0;
      // uint32_t n_epoch_local = 0;
      // uint32_t n_epoch_distributed = 0;

      do {
        int retry_cnt = 0;

        count++;
        process_request_lat.start();
        process_request();
        process_request_lat.end();
        std::chrono::steady_clock::time_point init_start;
        int64_t init_latency = 0;
        if (!partitioner->is_backup()) {
          // backup node stands by for replication
          last_seed = random.get_seed();
          init_start = std::chrono::steady_clock::now();
          if (retry_transaction) {
            retry_cnt++;
            transaction->reset();
          } else {

            auto partition_id = get_partition_id();

            transaction =
                workload.next_transaction(context, partition_id, storage);
            setupHandlers(*transaction);
          }
          init_latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - init_start).count();
          auto execution_start = std::chrono::steady_clock::now();
          auto result = transaction->execute(id);
          int64_t execution_latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - execution_start).count();
          if (result == TransactionResult::READY_TO_COMMIT) {
            bool commit =
                protocol.commit(*transaction, sync_messages, async_messages);
            n_network_size.fetch_add(transaction->network_size);
            if (commit) {
              // n_epoch_commit += 1;

              n_commit.fetch_add(1);
              if (transaction->si_in_serializable) {
                n_si_in_serializable.fetch_add(1);
              }
              if (transaction->local_validated) {
                n_local.fetch_add(1);
              }

              auto txn_lat =
                  std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now() - transaction->startTime)
                      .count();
              write_latency.add(txn_lat);
              if (transaction->distributed_transaction) {
                dist_latency.add(txn_lat);
                // n_epoch_distributed += 1;
              } else {
                local_latency.add(txn_lat);
                // n_epoch_local += 1;
              }
              execution_lat.add(execution_latency - transaction->get_process_request_lat() - transaction->get_sync_message_lat() + init_latency);
              sync_message_lat.add(transaction->get_sync_message_lat());
              if (transaction->get_process_request_lat() != 0) {
                process_request2_lat.add(transaction->get_process_request_lat());
              }
              prepare_lat.add(std::chrono::duration_cast<std::chrono::nanoseconds>(transaction->commitStartTime - transaction->prepareStartTime).count());
              commit_lat.add(std::chrono::duration_cast<std::chrono::nanoseconds>(transaction->commitEndTime - transaction->commitStartTime).count());
              retry_transaction = false;
              q.push(std::move(transaction));
            } else {
              // n_epoch_abort += 1;
              if (transaction->abort_lock) {
                n_abort_lock.fetch_add(1);
              } else {
                DCHECK(transaction->abort_read_validation);
                n_abort_read_validation.fetch_add(1);
              }
              // transaction->n_aborted += 1;
              sleep_on_retry_lat.start();
              if (context.sleep_on_retry) {
                std::this_thread::sleep_for(std::chrono::microseconds(
                    random.uniform_dist(0, context.sleep_time)));
              }
              sleep_on_retry_lat.end();
              abort_execution_lat.add(execution_latency - transaction->get_process_request_lat() - transaction->get_sync_message_lat() + init_latency);
              sync_message_lat.add(transaction->get_sync_message_lat());
              if (transaction->get_process_request_lat() != 0) {
                process_request2_lat.add(transaction->get_process_request_lat());
              }
              abort_prepare_lat.add(std::chrono::duration_cast<std::chrono::nanoseconds>(transaction->commitStartTime - transaction->prepareStartTime).count());
              random.set_seed(last_seed);
              retry_transaction = true;
            }
          } else {
            // n_epoch_abort += 1;
            protocol.abort(*transaction, sync_messages, async_messages);
            n_abort_no_retry.fetch_add(1);
          }

          if (count % context.batch_flush == 0) {
            flush_async_messages();
          } 
        }
        status = static_cast<ExecutorStatus>(worker_status.load());

        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - time)
                           .count() > 3000 && id == 0) {
        //   LOG(INFO) << "Worker " << id << " latency: " << commit_latency.aver() << " us (aver) " << commit_latency.sum()
        //       << " us (sum).";
        // LOG(INFO) << "write latency: " << write_latency.aver() << " us (aver) " << write_latency.sum()
        //       << " us (sum).";
        // LOG(INFO) << "dist latency: " << dist_latency.aver() << " us (aver) " << dist_latency.sum()
        //       << " us (sum).";
        // LOG(INFO) << "local latency: " << local_latency.aver() << " us (aver) " << local_latency.sum()
        //       << " us (sum).";
        LOG(INFO) << "execution latency: " << execution_lat.aver() << " us (aver) " << execution_lat.sum()
              << " us (sum). " << execution_lat.size();
        LOG(INFO) << "prepare latency: " << prepare_lat.aver() << " us (aver) " << prepare_lat.sum()
              << " us (sum). " << prepare_lat.size();
        LOG(INFO) << "abort_execution latency: " << abort_execution_lat.aver() << " us (aver) " << abort_execution_lat.sum()
              << " us (sum). " << abort_execution_lat.size();
        LOG(INFO) << "abort_prepare latency: " << abort_prepare_lat.aver() << " us (aver) " << abort_prepare_lat.sum()
              << " us (sum). " << abort_prepare_lat.size();
        LOG(INFO) << "commit latency: " << commit_lat.aver() << " us (aver) " << commit_lat.sum()
              << " us (sum). " << commit_lat.size();
        LOG(INFO) << "sleep_on_retry latency: " << sleep_on_retry_lat.aver() << " us (aver) " << sleep_on_retry_lat.sum()
              << " us (sum). " << sleep_on_retry_lat.size();
        LOG(INFO) << "process_request latency: " << process_request_lat.aver() << " us (aver) " << process_request_lat.sum()
              << " us (sum). " << process_request_lat.size();
        LOG(INFO) << "sync_message latency: " << sync_message_lat.aver() << " us (aver) " << sync_message_lat.sum()
              << " us (sum). " << sync_message_lat.size();
        LOG(INFO) << "process_request2 latency: " << process_request2_lat.aver() << " us (aver) " << process_request2_lat.sum()
              << " us (sum). " << process_request2_lat.size();
        LOG(INFO) << "total_execution latency: " << total_execution_lat.aver() << " us (aver) " << total_execution_lat.sum()
              << " us (sum). " << total_execution_lat.size();

          time = std::chrono::steady_clock::now();
          execution_lat.clear();
          prepare_lat.clear();
          abort_execution_lat.clear();
          abort_prepare_lat.clear();
          commit_lat.clear();
          sleep_on_retry_lat.clear();
          process_request_lat.clear();
          sync_message_lat.clear();
          process_request2_lat.clear();
          total_execution_lat.clear();
        }
      } while (status != ExecutorStatus::STOP);

      // static __thread int cc = 0;
      // if (++cc % 40 == 0) {
      //   LOG(INFO) << n_epoch_abort << " " << n_epoch_commit 
      //     << " " << n_epoch_local << " " << n_epoch_distributed;
      // }

      flush_async_messages();

      n_complete_workers.fetch_add(1);

      // once all workers are stop, we need to process the replication
      // requests
      process_request_lat.start();
      while (static_cast<ExecutorStatus>(worker_status.load()) !=
             ExecutorStatus::CLEANUP) {
        process_request();
      }

      process_request();
      process_request_lat.end();
      n_complete_workers.fetch_add(1);
    }
  }

  void onExit() override {
    
    LOG(INFO) << "Worker " << id << " latency: " << commit_latency.aver() << " us (aver) " << commit_latency.sum()
              << " us (sum).";
    LOG(INFO) << "write latency: " << write_latency.aver() << " us (aver) " << write_latency.sum()
              << " us (sum).";
    LOG(INFO) << "dist latency: " << dist_latency.aver() << " us (aver) " << dist_latency.sum()
              << " us (sum).";
    LOG(INFO) << "local latency: " << local_latency.aver() << " us (aver) " << local_latency.sum()
              << " us (sum).";
    LOG(INFO) << "execution latency: " << execution_lat.aver() << " us (aver) " << execution_lat.sum()
              << " us (sum).";
    LOG(INFO) << "prepare latency: " << prepare_lat.aver() << " us (aver) " << prepare_lat.sum()
              << " us (sum).";
    LOG(INFO) << "commit latency: " << commit_lat.aver() << " us (aver) " << commit_lat.sum()
              << " us (sum).";
    LOG(INFO) << "abort latency: " << abort_lat.aver() << " us (aver) " << abort_lat.sum()
              << " us (sum).";

    if (id == 0) {
      for (auto i = 0u; i < message_stats.size(); i++) {
        LOG(INFO) << "message stats, type: " << i
                  << " count: " << message_stats[i]
                  << " total size: " << message_sizes[i];
      }
      write_latency.save_cdf(context.cdf_path);
    }
  }

  std::size_t get_partition_id() {

    std::size_t partition_id;

    if (context.partitioner == "pb") {
      partition_id = random.uniform_dist(0, context.partition_num - 1);
    } else {
      auto partition_num_per_node =
          context.partition_num / context.coordinator_num;
      partition_id = random.uniform_dist(0, partition_num_per_node - 1) *
                         context.coordinator_num +
                     coordinator_id;
    }
    CHECK(partitioner->has_master_partition(partition_id));
    return partition_id;
  }

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
        ITable *table = db.find_table(messagePiece.get_table_id(),
                                      messagePiece.get_partition_id());

        messageHandlers[type](messagePiece,
                              *sync_messages[message->get_source_node_id()],
                              *table, transaction.get());
        message_stats[type]++;
        message_sizes[type] += messagePiece.get_message_length();
      }

      size += message->get_message_count();
      flush_sync_messages();
    }
    return size;
  }

  virtual void setupHandlers(TransactionType &txn) = 0;

protected:
  void flush_messages(std::vector<std::unique_ptr<Message>> &messages) {
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

  void flush_sync_messages() { flush_messages(sync_messages); }

  void flush_async_messages() { flush_messages(async_messages); }

  void init_message(Message *message, std::size_t dest_node_id) {
    message->set_source_node_id(coordinator_id);
    message->set_dest_node_id(dest_node_id);
    message->set_worker_id(id);
  }

protected:
  DatabaseType &db;
  const ContextType &context;
  std::atomic<uint32_t> &worker_status;
  std::atomic<uint32_t> &n_complete_workers, &n_started_workers;
  std::unique_ptr<Partitioner> partitioner;
  RandomType random;
  ProtocolType protocol;
  WorkloadType workload;
  std::unique_ptr<Delay> delay;
  Percentile<int64_t> commit_latency, write_latency, while_latency;
  Percentile<int64_t> dist_latency, local_latency;
  Percentile<int64_t> execution_lat, prepare_lat, commit_lat, abort_lat, sleep_on_retry_lat;
  Percentile<int64_t> abort_execution_lat, abort_prepare_lat;
  std::unique_ptr<TransactionType> transaction;
  std::vector<std::unique_ptr<Message>> sync_messages, async_messages;
  std::vector<
      std::function<void(MessagePiece, Message &, ITable &, TransactionType *)>>
      messageHandlers;
  std::vector<std::size_t> message_stats, message_sizes;
  LockfreeQueue<Message *> in_queue, out_queue;
};
} // namespace group_commit

} // namespace coco