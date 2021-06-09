#ifndef CHIAGEN_JOB
#define CHIAGEN_JOB

#include <string>
#include <vector>
#include <memory>

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
