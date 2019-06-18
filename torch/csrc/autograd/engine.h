#pragma once

// Engine implements backpropagation from output variables and their gradients
// to "root" variables (variables created by the user with requires_grad=True).

#include <torch/csrc/WindowsTorchApiMacro.h>
#include <torch/csrc/autograd/function.h>
#include <torch/csrc/autograd/input_buffer.h>
#include <torch/csrc/autograd/anomaly_mode.h>

#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace torch { namespace autograd {
struct ReadyQueue;
struct FunctionTask;
struct GraphTask;
}} // namespace torch::autograd

namespace torch { namespace autograd {
// A single instance of this struct should be created through the whole process lifetime.
// The worker thread creation logic and Engine's destructor rely on this.
struct TORCH_API Engine {
  /// Returns a reference to a static `Engine` instance.
  static Engine& get_default_engine();

  Engine();
  virtual ~Engine();

  using ready_queue_type = std::deque<std::pair<std::shared_ptr<Function>, InputBuffer>>;
  using dependencies_type = std::unordered_map<Function*, int>;

  // Given a list of (Function, input number) pairs computes the value of the graph
  // by following next_edge references.
  virtual variable_list execute(
      const edge_list& roots,
      const variable_list& inputs,
      bool keep_graph,
      bool create_graph,
      const edge_list& outputs = {});
  virtual std::unique_ptr<AnomalyMetadata> make_anomaly_metadata() {
    return nullptr;
  }

  void queue_callback(std::function<void()> callback);

  bool is_checkpoint_valid();

protected:
  void compute_dependencies(Function* root, GraphTask& task);
  void evaluate_function(FunctionTask& task);
  ReadyQueue& ready_queue(at::Device device);
  ReadyQueue& ready_queue_by_index(int device_index);
  void start_threads();
  virtual void thread_init(int device);
  virtual void thread_main(std::shared_ptr<GraphTask> graph_task);
  virtual void thread_on_exception(FunctionTask& task, std::exception& e);
  void reentrant_thread_init();
  void process_queue(int device);

  // Ensures ready_queues_ are initialized only once
  std::once_flag start_threads_flag_;
  // Safe to read ready_queues_ without synchronization after intialization
  std::vector<std::shared_ptr<ReadyQueue>> ready_queues_;
  std::vector<std::function<void()>> final_callbacks_;
  // To protect reads and writes to final_callbacks_
  std::mutex post_callbacks_lock_;

  struct ThreadPoolShared {
    std::atomic_int num_workers_;
    std::condition_variable work_;
    std::mutex graphtasks_queue_lock_;
    std::queue<std::shared_ptr<GraphTask>> graphtasks_queue_;

    ThreadPoolShared() : num_workers_(0) {};
 };

 // Temporary workaround until shutting down threads is done
 // Need shared ownership of all these objects because threads are leaked
 // when Engine shuts down
 std::shared_ptr<ThreadPoolShared> thread_pool_shared_;
 // TODO don't really need this because the parent thread waits
 std::vector<std::shared_ptr<std::mutex>> device_locks_;
};

// allow python_engine to override the default engine when it loads
using EngineStub = Engine& (*)();
TORCH_API void set_default_engine_stub(EngineStub stub);

}} // namespace torch::autograd
