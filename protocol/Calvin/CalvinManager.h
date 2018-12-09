//
// Created by Yi Lu on 9/13/18.
//

#pragma once

#include "core/Manager.h"
#include "core/Partitioner.h"
#include "protocol/Calvin/Calvin.h"
#include "protocol/Calvin/CalvinExecutor.h"
#include "protocol/Calvin/CalvinHelper.h"
#include "protocol/Calvin/CalvinTransaction.h"

#include <thread>
#include <vector>

namespace scar {

template <class Workload> class CalvinManager : public scar::Manager {
public:
  using base_type = scar::Manager;

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

  CalvinManager(std::size_t coordinator_id, std::size_t id, DatabaseType &db,
                const ContextType &context, std::atomic<bool> &stopFlag)
      : base_type(coordinator_id, id, context, stopFlag), db(db),
        workload_context(context),
        partitioner(coordinator_id, context.coordinator_num,
                    CalvinHelper::string_to_vint(context.replica_group)),
        workload(coordinator_id, db, random, partitioner) {

    storages.resize(context.batch_size);
  }

  void coordinator_start() override {

    std::size_t n_workers = context.worker_num;
    std::size_t n_coordinators = context.coordinator_num;

    while (!stopFlag.load()) {

      // the coordinator on each machine generates
      // a batch of transactions using the same random seed.

      // LOG(INFO) << "Seed: " << random.get_seed();
      prepare_transactions();

      n_started_workers.store(0);
      n_completed_workers.store(0);
      signal_worker(ExecutorStatus::Analysis);
      // Allow each worker to analyse the read/write set
      // each worker analyse i, i + n, i + 2n transaction
      wait_all_workers_start();
      wait_all_workers_finish();

      // wait for all machines until they finish the analysis phase.
      wait4_ack();

      // Allow each worker to run transactions
      // DB is partitioned by the number of lock managers.
      // The first k workers act as lock managers to grant locks to other
      // workers The remaining workers run transactions upon assignment via the
      // queue.
      n_started_workers.store(0);
      n_completed_workers.store(0);
      complete_transaction_num.store(0);
      signal_worker(ExecutorStatus::Execute);
      wait_all_workers_start();
      wait_all_workers_finish();
      wait_transaction_complete();

      // wait for all machines until they finish the execution phase.
      wait4_ack();
    }

    signal_worker(ExecutorStatus::EXIT);
  }

  void non_coordinator_start() override {

    std::size_t n_workers = context.worker_num;
    std::size_t n_coordinators = context.coordinator_num;

    for (;;) {
      // LOG(INFO) << "Seed: " << random.get_seed();
      ExecutorStatus status = wait4_signal();
      if (status == ExecutorStatus::EXIT) {
        set_worker_status(ExecutorStatus::EXIT);
        break;
      }

      DCHECK(status == ExecutorStatus::Analysis);
      // the coordinator on each machine generates
      // a batch of transactions using the same random seed.
      prepare_transactions();

      // Allow each worker to analyse the read/write set
      // each worker analyse i, i + n, i + 2n transaction

      n_started_workers.store(0);
      n_completed_workers.store(0);
      set_worker_status(ExecutorStatus::Analysis);
      wait_all_workers_start();
      wait_all_workers_finish();

      send_ack();

      status = wait4_signal();
      DCHECK(status == ExecutorStatus::Execute);
      // Allow each worker to run transactions
      // DB is partitioned by the number of lock managers.
      // The first k workers act as lock managers to grant locks to other
      // workers The remaining workers run transactions upon assignment via the
      // queue.
      n_started_workers.store(0);
      n_completed_workers.store(0);
      complete_transaction_num.store(0);
      set_worker_status(ExecutorStatus::Execute);
      wait_all_workers_start();
      wait_all_workers_finish();
      wait_transaction_complete();

      send_ack();
    }
  }

  void add_worker(const std::shared_ptr<CalvinExecutor<WorkloadType>> &w) {
    workers.push_back(w);
  }

  void prepare_transactions() {

    transactions.clear();

    // generate transactions
    for (auto i = 0u; i < context.batch_size; i++) {
      auto partition_id = random.uniform_dist(0, context.partition_num - 1);
      transactions.push_back(workload.next_transaction(
          workload_context, partition_id, storages[i]));
      transactions[i]->set_id(i);
    }
  }

  void wait_transaction_complete() {
    while (complete_transaction_num.load() < transactions.size()) {
      std::this_thread::yield();
    }
  }

public:
  RandomType random;
  DatabaseType &db;
  const ContextType &workload_context;
  CalvinPartitioner partitioner;
  WorkloadType workload;
  std::atomic<uint32_t> complete_transaction_num;
  std::vector<std::shared_ptr<CalvinExecutor<WorkloadType>>> workers;
  std::vector<StorageType> storages;
  std::vector<std::unique_ptr<TransactionType>> transactions;
};
} // namespace scar