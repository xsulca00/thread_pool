#include <thread>
#include <vector>
#include <iostream>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <future>

using namespace std;
using namespace std::chrono;

class ScopedThread {
public:
	template<typename F, typename... Args>
	explicit ScopedThread(F&& f, Args&&... args) 
		: _thread{forward<F>(f), forward<Args>(args)...} {}
		
	ScopedThread() = default;
	ScopedThread(ScopedThread&&) = default;

	ScopedThread& operator=(ScopedThread&& t) {
		if (joinable()) join();
		_thread = move(t._thread);
		return *this;
	}

	bool joinable() const noexcept { return _thread.joinable(); }
	thread::id get_id() const noexcept { return _thread.get_id(); }
	thread::native_handle_type native_handle() { return _thread.native_handle(); }

	void join() { _thread.join(); }
	void detach() { _thread.detach(); }
	void swap(ScopedThread& t) noexcept { _thread.swap(t._thread); }

	~ScopedThread() { if (joinable()) join(); }
private:
	thread _thread;
};

template<typename T>
class SyncedQueue {
public:
	using value_type = T;

	void Push(const value_type& v) {
		scoped_lock l {_mutex};
		_queue.push(v);
		_cv.notify_one();
	}

	void Push(value_type&& v) {
		scoped_lock l {_mutex};
		_queue.push(move(v));
		_cv.notify_one();
	}

	void Pop(value_type& v) {
		unique_lock l {_mutex};
		_cv.wait(l, [this]{ return !_queue.empty(); });
		v = move(_queue.front());
		_queue.pop();
	}

	bool TryPop(value_type& v) {
		scoped_lock l {_mutex};
		if (_queue.empty()) return false;
		v = move(_queue.front());
		_queue.pop();
		return true;
	}

	bool PopTimeOut(value_type& v, steady_clock::duration d) {
		unique_lock l {_mutex};
		bool notEmpty {_cv.wait_for(l, d, [this]{ return !_queue.empty(); })};
		if (notEmpty) {
			v = move(_queue.front());
			_queue.pop();
			return true;
		}
		return false;
	}

private:
	mutex _mutex;
	condition_variable _cv;
	queue<value_type> _queue;
};


struct func_base {
	virtual void call() = 0;
	// by removing virtual destructor, you will get a speedup
	virtual ~func_base() {}
};

template<typename F>
class func : public func_base {
public:
	func(packaged_task<F()> ff) : f{move(ff)} {}

	void call() override { f(); }
private:
	packaged_task<F()> f;
};

class function_wrapper {
public:
	function_wrapper() = default;

	template<typename F>
	function_wrapper(packaged_task<F()> f) : ffunc{ make_unique<func<F>>(move(f)) } {}

	void operator()() { ffunc->call(); }
private:
	unique_ptr<func_base> ffunc;
};

class ThreadPool {
public:
	ThreadPool(int count) try {
		if (count <= 0) throw runtime_error{"ThreadPool::ThreadPool(): count <= 0"};

		for (int i {0}; i != count; ++i) {
			_workers.emplace_back([this]{ Run(); });
		}
	} catch (...) {
		_done = true;
		throw;
	}

	~ThreadPool() { _done = true; }

	template<typename F>
	future<invoke_result_t<F>> AddTask(F&& fun) { 
		packaged_task<invoke_result_t<F>()> task {forward<F>(fun)};
		auto fut {task.get_future()};
		_queue.Push(move(task)); 
		return fut;
	}

private:

	void Run() {
		while (!_done) {
			function_wrapper task;
			if (_queue.TryPop(task)) {
			   	task();
			} else {
				this_thread::yield();
			}
		}
	} 

	SyncedQueue<function_wrapper> _queue;
	atomic_bool _done {false};
	vector<ScopedThread> _workers;
};
	
int main() {
	ThreadPool p {4};

	future<int> f1 {p.AddTask([]{ return 4; })};
	future<string> f2 {p.AddTask([]{ return "ahoj"s; })};
	future<unique_ptr<double>> f3 {p.AddTask([]{ return make_unique<double>(5); })};

	cout << f1.get() << '\n';
	cout << f2.get() << '\n';
	cout << *f3.get() << '\n';
}
