#ifndef CHIAGEN_JOB
#define CHIAGEN_JOB

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <mutex>

#define NOMINMAX
#include <windows.h> 
#include <thread>
#include <functional>

class Job;
class JobEvent;

class JobEventId {
public:
	std::string name;
	std::string type;
	bool isEmpty();
	bool match(JobEvent* event);
};

class JobEvent : public std::enable_shared_from_this<JobEvent> {
public:
	JobEvent(std::string type);
	virtual void trigger(std::shared_ptr<Job> source);
	std::string getType() const;
	std::string name;
protected:	
	std::string type;
};

class JobRule {
public:
	JobRule(){};
	virtual bool evaluate(){ return true; };
	virtual void handleEvent(std::shared_ptr<JobEvent> jobEvent, std::shared_ptr<Job> source){}
	virtual bool drawItemWidget() { return false; };
	virtual void update(){}
};

class JobStartRule : public JobRule {
public:
	
};

class JobFinishRule : public JobRule {
public:

};

class JobActivityState {
public:
	bool paused {false};
	bool running {false};
	bool finished {false};
	bool cancel {false};
};


class JobTaskItem : public std::enable_shared_from_this<JobTaskItem> {
public:
	JobTaskItem(std::string name);
	virtual void addChild(std::shared_ptr<JobTaskItem> task);
	virtual void removeChild(std::shared_ptr<JobTaskItem> task);
	virtual float getProgress();
	virtual uint32_t getTotalWorkItems();
	virtual uint32_t getCompletedWorkItems();
	virtual bool start();
	virtual bool stop(bool finished = true);	
	bool isRunning() const;
	bool isFinished() const;
	std::string name;
	std::string status;
	uint32_t completedWorkItem {0};
	uint32_t totalWorkItem {0};
	virtual bool drawStatusWidget();
	std::chrono::time_point<std::chrono::system_clock> startTime;
	std::chrono::time_point<std::chrono::system_clock> finishTime;
	std::vector<std::shared_ptr<JobTaskItem>> tasks;
	std::shared_ptr<JobTaskItem> parentTask {nullptr};
protected:
	JobActivityState state;
};

class JobActvity : public JobTaskItem{
public:
	JobActvity(std::string name, Job* owner);
	bool start() override;
	bool start(uint32_t sampleCount);
	bool stop(bool finished, bool force);
	bool stop(bool finished = true) override;
	bool pause(bool isPause = true);
	bool isPaused() const;	
	virtual bool drawStatusWidget() override;

	void samplerUpdate();
	std::vector<float> cpuKernelTimeHistory;
	std::vector<float> cpuUserTimeHistory;
	Job* job {nullptr};
	std::function<void(JobActivityState*)> mainRoutine;
	std::function<void(JobActivityState*)> onPause;
	std::function<void(JobActivityState*)> onResume;
	std::function<void(JobActivityState*)> onFinish;
	void waitUntilFinish();
	std::mutex mutex;
protected:	
	void drawPlot();
	std::vector<float> xAxis;
	std::chrono::time_point<std::chrono::steady_clock> lastSampleTime;
	std::thread jobThread {};		
	uint64_t totalKernelTime{0};
	uint64_t totalUserTime{0};	
	uint32_t statSampleCount {100};
	bool stopActivity(bool finished);

	void collectCPUUsage();
};

class Job : public std::enable_shared_from_this<Job>{
public:
	Job(std::string title);
	virtual bool isRunning() const;
	virtual bool isPaused() const;
	virtual bool isFinished() const;	
	virtual bool start(bool overrideRule = false);
	virtual bool pause();
	virtual bool cancel(bool forced = true);
	virtual float getProgress();
	virtual JobRule* getStartRule();
	virtual JobRule* getFinishRule();
	virtual ~Job(){};
	std::string getTitle() const;
	virtual bool drawEditor();
	virtual bool drawItemWidget();
	virtual bool drawStatusWidget();
	virtual void handleEvent(std::shared_ptr<JobEvent> jobEvent, std::shared_ptr<Job> source);
	virtual void update();
	void log(std::string text);
	std::vector<std::shared_ptr<JobEvent>> events;
	std::shared_ptr<JobActvity> activity;
	std::string logText;
protected:
	virtual void initActivity();
	std::string title;
	bool logScroll {false};
};

class JobFactory {
public:
	virtual std::string getName() = 0;
	virtual std::shared_ptr<Job> create(std::string jobName) = 0;
	virtual bool drawEditor() = 0;
};

template <typename F> class FactoryRegistration {
public:
	FactoryRegistration();
};

class JobManager {
public: 
	JobManager();
	static JobManager& getInstance();
	void triggerEvent(std::shared_ptr<JobEvent> jobEvent,std::shared_ptr<Job> source);
	void listEvents(std::vector<std::shared_ptr<JobEvent>> out);
	void addJob(std::shared_ptr<Job> newJob);
	void setSelectedJob(std::shared_ptr<Job> job);
	std::shared_ptr<Job> getSelectedJob() const;
	size_t countJob() const;
	size_t countRunningJob() const;
	std::vector<std::shared_ptr<Job>>::const_iterator jobIteratorBegin() const ;
	std::vector<std::shared_ptr<Job>>::const_iterator jobIteratorEnd() const ;
	void registerJobFactory(std::shared_ptr<JobFactory> factory);

	bool isRunning {true};
	void stop();
	void start();
	std::mutex mutex;
	std::vector<std::shared_ptr<JobFactory>> jobFactories;
	void log(std::string text, std::shared_ptr<Job> job = nullptr);
	void logErr(std::string text, std::shared_ptr<Job> job = nullptr);
protected:
	uint32_t statUpdateInterval {1};
	uint32_t statSampleCount {100};
	HANDLE myProcess {nullptr};
	uint64_t lastDiskWrite {0};
	uint64_t lastDiskRead {0};
	std::vector<uint64_t> diskWriteHistory;
	std::vector<uint64_t> diskReadHistory;
	std::vector<uint64_t> memUsageHistory;
	std::chrono::time_point<std::chrono::steady_clock> lastSampleTime;
	
	void collectMemUsage();  
	void collectDiskUsage(); 
	void collectPerfSample();
	void samplerThreadProc();

	std::vector<std::shared_ptr<Job>> activeJobs;
	std::shared_ptr<Job> selectedJob {nullptr};
	std::thread samplerThread {};
};

template <typename F>
FactoryRegistration<F>::FactoryRegistration()
{
	std::shared_ptr<JobFactory> factoryPtr = std::static_pointer_cast<JobFactory>(std::make_shared<F>());
	JobManager::getInstance().registerJobFactory(factoryPtr);
}

#endif
