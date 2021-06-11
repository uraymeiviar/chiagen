#ifndef CHIAGEN_JOB
#define CHIAGEN_JOB

#include <string>
#include <vector>
#include <memory>
#include <chrono>

#define NOMINMAX
#include <windows.h> 

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

class JobTaskItem {
public:
	JobTaskItem(std::string name, std::shared_ptr<JobTaskItem> parent = nullptr);
	virtual void addChild(std::shared_ptr<JobTaskItem> task);
	virtual void addDiskRead(uint64_t byteSize);
	virtual void addDiskWrite(uint64_t byteSize);
	virtual float getProgress();
	virtual uint32_t getTotalWorkItems();
	virtual uint32_t getCompletedWorkItems();
	virtual void start();
	virtual void stop(bool finished = true);
	bool isRunning() const;;
	bool isFinished() const;;
	std::string name;
	std::string status;
	uint32_t completedWorkItem {0};
	uint32_t totalWorkItem {0};
	std::chrono::time_point<std::chrono::system_clock> startTime;
	std::chrono::time_point<std::chrono::system_clock> finishTime;
	std::vector<std::shared_ptr<JobTaskItem>> tasks;
	std::shared_ptr<JobTaskItem> parentTask;
protected:
	bool running {false};
	bool finished {false};
};

class JobProgress : public JobTaskItem{
public:
	JobProgress(std::string name);
	void addDiskRead(uint64_t byteSize) override;
	void addDiskWrite(uint64_t byteSize) override;
	void start(HANDLE jobThread, uint32_t updateInterval = 10, uint32_t sampleCount = 60);
	void stop(bool finished, bool force = false);
	void pause(bool isPause = true);
	bool isPaused() const;
	void samplerUpdate();

	uint64_t diskWrite {0};
	uint64_t diskRead {0};

	std::vector<float> cpuKernelTimeHistory;
	std::vector<float> cpuUserTimeHistory;
	std::vector<uint64_t> diskWriteHistory;
	std::vector<uint64_t> diskReadHistory;
	std::vector<uint64_t> memUsageHistory;
protected:
	uint32_t statUpdateInterval {10};
	uint32_t statSampleCount {60};
	HANDLE jobThread {nullptr};
	HANDLE samplerThread {nullptr};
	HANDLE myProcess {nullptr};
	bool paused {false};
	uint64_t lastDiskWrite {0};
	uint64_t lastDiskRead {0};
	uint64_t totalKernelTime{0};
	uint64_t totalUserTime{0};
	std::chrono::time_point<std::chrono::steady_clock> lastSampleTime;

	void collectCPUUsage();
	void collectMemUsage();
	void collectDiskUsage();
};

class Job {
public:
	Job(std::string title):title(title){};
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
protected:
	std::string title;
};

class JobManager {
public: 
	JobManager(){};
	static JobManager& getInstance();
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
