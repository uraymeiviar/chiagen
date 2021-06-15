#ifndef TASK_H
#define TASK_H

#include <functional>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <deque>

class TaskRunnerPool;

class TaskRunner : public std::enable_shared_from_this<TaskRunner>{
public:
	TaskRunner( std::weak_ptr<TaskRunnerPool> pool, 
				std::function<void(std::shared_ptr<TaskRunner>)>) 
	: pool(pool){};
	void run(std::function<void()> cb) {
		try {
			cb();
		}
		catch (...) {}
		if (this->pool.lock()) {
			this->finishCb(this->shared_from_this());
		}
	}
protected:
	bool active {true};
	bool available {false};
	std::mutex mutex;
	std::thread thread;
	std::condition_variable signal;
	std::weak_ptr<TaskRunnerPool> pool;
	std::function<void(std::shared_ptr<TaskRunner>)> finishCb;
};

class TaskRunnerPool {
public:
	TaskRunnerPool(size_t threadCount = 2){};
	void run(std::function<void()> cb);
	void push_front(std::function<void()> cb);
	void push_back(std::function<void()> cb);
protected:	
	std::vector<std::shared_ptr<TaskRunner>> runners;
	std::deque<std::shared_ptr<TaskRunner>> availableRunners;
	std::deque<std::function<void()>> tasks;
};

template<typename ...Inp> class Proc {
public:
	Proc(std::function<void(Inp&...)> fn) : fn(fn){};
	virtual void process(const Inp&... inp){
		TaskRunner::run([=](){this->fn(Inp...)});
	};
protected:
	std::function<void(Inp&...)> fn;
};

template<typename Out, typename ...Inp> 
class Task : public Proc<std::function<void(Out)>, Inp...>{ //insert first template arg to baseclass template
public:
	typedef std::shared_ptr<Proc<Out>> NextTask;
	Task(std::function<void(std::function<void(Out)>, const Inp...)> fn, NextTask next = nullptr) 
	:Proc<Inp...>(
			[=](const Inp... inp){
				fn(
					[=](Out out) {
						if(this->next){
							this->next->process(out);
						}
					}, 
					inp...
				);
			}
		), 
		next(next)
	{};	
protected:
	NextTask next;
};

#endif // TASK_H