#include <thread>
#include <unistd.h>
#include <signal.h>
#include <vector>
#include <iostream>
#include <atomic>
#include <list>
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

	bool PopTimeOut(value_type& v, milliseconds ms) {
		unique_lock l {_mutex};
		if (_cv.wait_for(l, ms, [this]{ return !_queue.empty(); })) {
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

class ThreadPool {
public:
	ThreadPool(int count) try {
		for (int i {0}; i != count; ++i) {
			_workers.emplace_back([this]{ Run(); });
		}
	} catch (...) {
		_done = true;
		throw;
	}

	~ThreadPool() { _done = true; }

	template<typename F>
	future<void> AddTask(F fun) { 
		packaged_task<void()> task {fun};
		future<void> fut {task.get_future()};
		_queue.Push(move(task)); 
		return fut;
	}

private:

	void Run() {
		while (!_done) {
			packaged_task<void()> task;
			if (_queue.PopTimeOut(task, 1s)) task();
		}
	} 

	atomic_bool _done {false};
	SyncedQueue<packaged_task<void()>> _queue;
	// be careful with _workers destructor, if _queue is destructed before _workers are,
	// than worker thread can wait with timeout on dangling function pointer of _queue
	vector<ScopedThread> _workers;
};

	
int main() {
	ThreadPool p {4};

	vector<future<void>> v;

	for (int i {0}; i != 10'000; ++i) {
		v.emplace_back(p.AddTask([i]{ cout << i << '\n'; }));
	}

	for (auto& f : v) f.wait();
}
