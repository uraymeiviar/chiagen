#ifndef CHIAGEN_JOB_CREATEPLOT_H
#define CHIAGEN_JOB_CREATEPLOT_H
#include "Job.hpp"
#include "gui.hpp"
#include <chrono>
#include <filesystem>
#include <stack>
#include <thread>
#include "JobRule.h"


class CreatePlotContext {
public:
	void log(std::string text);
	void logErr(std::string text);
	std::shared_ptr<Job> job;
	std::shared_ptr<JobTaskItem> getCurrentTask();
	std::shared_ptr<JobTaskItem> pushTask(std::string name, std::shared_ptr<JobTaskItem> parent);
	std::shared_ptr<JobTaskItem> popTask(bool finish=true);
protected:
	std::stack<std::shared_ptr<JobTaskItem>> tasks;
};

class JobCreatePlot : public Job {
public:
	JobCreatePlot(std::string title, std::string originalTitle = "");
	JobCreatePlot(std::string title, std::string originalTitle,
		const JobStartRuleParam& startParam,
		const JobFinishRuleParam& finishParam
	);
	virtual ~JobCreatePlot(){};
	static int jobIdCounter;

	JobRule* getStartRule() override;
	JobRule* getFinishRule() override;

	virtual bool drawItemWidget() override;
	virtual bool drawStatusWidget() override;	
	virtual bool relaunchAfterFinish() override;

	std::shared_ptr<JobEvent> startEvent;
	std::shared_ptr<JobEvent> finishEvent;
	std::shared_ptr<JobEvent> phase1FinishEvent;
	std::shared_ptr<JobEvent> phase2FinishEvent;
	std::shared_ptr<JobEvent> phase3FinishEvent;
	std::shared_ptr<JobEvent> phase4FinishEvent;

protected:
	void init();
	JobStartRule startRule;
	JobFinishRule finishRule;
};

#endif