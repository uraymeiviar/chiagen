#ifndef _JOB_RULE_H_
#define _JOB_RULE_H_
#include "Job.hpp"
#include <filesystem>

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
	JobEvent(std::string type, std::string name);
	virtual void trigger(std::shared_ptr<Job> source);
	std::string getType() const;
	std::string name;
protected:	
	std::string type;
};


class JobRule {
public:
	JobRule(){};
	virtual ~JobRule(){};
	virtual bool evaluate(){ return true; };
	virtual void handleEvent(std::shared_ptr<JobEvent> jobEvent, std::shared_ptr<Job> source){}
	virtual bool drawItemWidget() { return false; };
	virtual void update(){}
};

class JobStartRuleParam{
public:
	bool startImmediate {true};
	bool startDelayed {false};
	bool startPaused {false};
	bool startConditional {false};
	bool startCondActiveJob {true};
	bool startCondTime {false};
	bool startOnEvent {false};
	int startDelayedMinute {15};
	int startCondTimeStart {1};
	int startCondTimeEnd {6};
	int startCondActiveJobCount {1};
	JobEventId eventToRespond;
	virtual bool drawEditor();
};

class JobStartRule : public JobRule {
public:
	JobStartRule();
	JobStartRule(const JobStartRuleParam& param);
	virtual ~JobStartRule(){};
	virtual bool drawEditor();
	virtual bool drawItemWidget();
	bool evaluate() override;
	void handleEvent(std::shared_ptr<JobEvent> jobEvent, std::shared_ptr<Job> source) override;
	std::chrono::time_point<std::chrono::system_clock> creationTime;
	std::function<void()> onStartTrigger;
	JobStartRuleParam& getParam();
	JobStartRuleParam& getRelaunchParam();
protected:
	bool isRuleFullfilled();
	JobStartRuleParam param;
};

class JobFinishRuleParam {
public:
	bool repeatJob {false};
	bool repeatIndefinite {false};
	bool execProg {false};
	std::filesystem::path progToExec;
	std::filesystem::path progWorkingDir;
	bool execProgOnRepeat {true};
	int repeatCount {1};

	virtual bool drawEditor();
};

class JobFinishRule : public JobRule {
public:
	JobFinishRule();
	JobFinishRule(const JobFinishRuleParam& param);
	virtual ~JobFinishRule(){};
	virtual bool drawEditor();
	virtual bool drawItemWidget();
	virtual bool relaunchAfterFinish();
	JobFinishRuleParam& getParam();
	JobFinishRuleParam& getRelaunchParam();
protected:
	JobFinishRuleParam param;
};

#endif
