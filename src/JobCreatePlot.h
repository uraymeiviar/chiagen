#ifndef CHIAGEN_JOB_CREATEPLOT_H
#define CHIAGEN_JOB_CREATEPLOT_H
#include "Job.hpp"
#include "gui.hpp"
#include <chrono>
#include <filesystem>
#include <stack>
#include <thread>

class JobCratePlotStartRuleParam{
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

	virtual bool drawEditor();
};

class JobCratePlotStartRule : public JobStartRule {
public:
	JobCratePlotStartRule();
	JobCratePlotStartRule(const JobCratePlotStartRuleParam& param);
	virtual bool drawEditor();
	virtual bool drawItemWidget();
	bool evaluate() override;
	void handleEvent(std::shared_ptr<JobEvent> jobEvent, std::shared_ptr<Job> source) override;
	std::chrono::time_point<std::chrono::system_clock> creationTime;
	std::function<void()> onStartTrigger;
protected:
	bool isRuleFullfilled();
	JobCratePlotStartRuleParam param;
	JobEventId waitForEvent;	
};

class JobCreatePlotFinishRuleParam {
public:
	bool repeatJob {true};
	bool repeatIndefinite {false};
	bool execProg {false};
	std::filesystem::path progToExec;
	std::filesystem::path progWorkingDir;
	bool execProgOnRepeat {true};
	int repeatCount {1};

	virtual bool drawEditor();
};

class JobCreatePlotFinishRule : public JobFinishRule {
public:
	JobCreatePlotFinishRule();
	JobCreatePlotFinishRule(const JobCreatePlotFinishRuleParam& param);
	virtual bool drawEditor();
	virtual bool drawItemWidget();
protected:
	JobCreatePlotFinishRuleParam param;
};

class CreatePlotContext {
public:
	std::shared_ptr<Job> job;
	std::shared_ptr<JobTaskItem> getCurrentTask();
	std::shared_ptr<JobTaskItem> pushTask(std::string name, std::shared_ptr<JobTaskItem> parent);
	std::shared_ptr<JobTaskItem> popTask(bool finish=true);
protected:
	std::stack<std::shared_ptr<JobTaskItem>> tasks;
};

class JobCreatePlot : public Job {
public:
	JobCreatePlot(std::string title);
	JobCreatePlot(std::string title, 
		const JobCratePlotStartRuleParam& startParam,
		const JobCreatePlotFinishRuleParam& finishParam
	);
	static int jobIdCounter;

	JobRule* getStartRule() override;
	JobRule* getFinishRule() override;

	virtual bool drawItemWidget() override;
	virtual bool drawStatusWidget() override;

	std::shared_ptr<JobEvent> startEvent;
	std::shared_ptr<JobEvent> finishEvent;
	std::shared_ptr<JobEvent> phase1FinishEvent;
	std::shared_ptr<JobEvent> phase2FinishEvent;
	std::shared_ptr<JobEvent> phase3FinishEvent;
	std::shared_ptr<JobEvent> phase4FinishEvent;

protected:
	void init();
	bool paused {false};
	bool running {false};
	bool finished {false};
	float progress {0.0f};
	JobCratePlotStartRule startRule;
	JobCreatePlotFinishRule finishRule;
};

#endif