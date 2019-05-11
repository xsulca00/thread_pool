#include <thread>
#include <unistd.h>
#include <signal.h>
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

// TODO: Synced queue

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
		packaged_task<void()> pt {fun};
		future<void> fut {pt.get_future()};
		_queue.Push(move(pt)); 
		return fut;
	}
private:
	void Run() try {
		while (!_done) {
			packaged_task<void()> pt;
			if (_queue.PopTimeOut(pt, 1s)) pt();
		}
	} catch (const exception& e) {
		cerr << e.what() << '\n';
	}

	atomic_bool _done {false};
	vector<ScopedThread> _workers;
	SyncedQueue<packaged_task<void()>> _queue;
};

void func() {
	cout << 5 << '\n';
}
	
int main() try {
	ThreadPool p {4};

	for (int i {0}; i != 10'000; ++i) {
		p.AddTask(func);
	}

} catch (const exception& e) {
	cerr << e.what() << '\n';
}
