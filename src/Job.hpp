#ifndef CHIAGEN_JOB
#define CHIAGEN_JOB

#include <string>
#include <vector>
#include <memory>
#include <chrono>

#define NOMINMAX
#include <windows.h> 
#include <thread>
#include <functional>

class JobRule {
public:
	JobRule(){};
	virtual bool evaluate(){ return true; };
	virtual bool drawItemWidget() { return false; };
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
	virtual void start();
	virtual void stop(bool finished = true);
	bool isRunning() const;
	bool isFinished() const;
	std::string name;
	std::string status;
	uint32_t completedWorkItem {0};
	uint32_t totalWorkItem {0};
	std::chrono::time_point<std::chrono::system_clock> startTime;
	std::chrono::time_point<std::chrono::system_clock> finishTime;
	std::vector<std::weak_ptr<JobTaskItem>> tasks;
	std::shared_ptr<JobTaskItem> parentTask {nullptr};
protected:
	JobActivityState state;
};

class Job;

class JobActvity : public JobTaskItem{
public:
	JobActvity(std::string name);
	void start(uint32_t updateInterval = 10, uint32_t sampleCount = 60);
	void stop(bool finished, bool force = false);
	void pause(bool isPause = true);
	bool isPaused() const;	

	uint64_t diskWrite {0};
	uint64_t diskRead {0};

	std::vector<float> cpuKernelTimeHistory;
	std::vector<float> cpuUserTimeHistory;
	std::vector<uint64_t> diskWriteHistory;
	std::vector<uint64_t> diskReadHistory;
	std::vector<uint64_t> memUsageHistory;
	Job* job {nullptr};
	std::function<void(JobActivityState*)> mainRoutine;
protected:
	uint32_t statUpdateInterval {10};
	uint32_t statSampleCount {60};
	std::thread jobThread {};
	std::thread samplerThread {};
	HANDLE myProcess {nullptr};
	uint64_t lastDiskWrite {0};
	uint64_t lastDiskRead {0};
	uint64_t totalKernelTime{0};
	uint64_t totalUserTime{0};
	std::chrono::time_point<std::chrono::steady_clock> lastSampleTime;

	void collectCPUUsage();
	void collectMemUsage();  //TODO:move to jobmanager
	void collectDiskUsage(); //TODO:move to jobmanager
	void samplerUpdate();
	void samplerThreadProc();
};

class JobEvent : public std::enable_shared_from_this<JobEvent> {
public:
	JobEvent(std::string type);;
	virtual void trigger();
	std::string getType() const;;
	std::string name;
protected:	
	std::string type;
};

class Job {
public:
	Job(std::string title);
	virtual bool isRunning() const = 0;
	virtual bool isPaused() const = 0;
	virtual bool isFinished() const = 0;
	virtual bool start() = 0;
	virtual bool pause() = 0;
	virtual bool cancel() = 0;
	virtual float getProgress() = 0;
	virtual JobRule& getStartRule() = 0;
	virtual JobRule& getFinishRule() = 0;
	virtual ~Job(){};
	std::string getTitle() const;
	virtual void drawItemWidget();
	virtual void drawStatusWidget();
	virtual void handleEvent(std::shared_ptr<JobEvent> jobEvent){}
	std::vector<std::shared_ptr<JobEvent>> events;
	std::shared_ptr<JobActvity> activity;
protected:
	std::string title;
};

class JobManager {
public: 
	JobManager(){};
	static JobManager& getInstance();
	void triggerEvent(std::shared_ptr<JobEvent> jobEvent);
	void listEvents(std::vector<std::shared_ptr<JobEvent>> out);
	void addJob(std::shared_ptr<Job> newJob);
	void setSelectedJob(std::shared_ptr<Job> job);
	std::shared_ptr<Job> getSelectedJob() const;
	size_t countJob() const;
	size_t countRunningJob() const;
	std::vector<std::shared_ptr<Job>>::const_iterator jobIteratorBegin() const ;
	std::vector<std::shared_ptr<Job>>::const_iterator jobIteratorEnd() const ;
protected:
	std::vector<std::shared_ptr<Job>> activeJobs;
	std::shared_ptr<Job> selectedJob {nullptr};
};

#endif
