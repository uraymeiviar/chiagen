#ifndef CHIAGEN_JOB_CREATEPLOT_H
#define CHIAGEN_JOB_CREATEPLOT_H
#include "Job.hpp"
#include "gui.hpp"
#include <chrono>
#include <filesystem>
#include <stack>
#include <thread>

class JobCratePlotStartRuleParam {
public:
	JobCratePlotStartRuleParam();
	bool startImmediate {true};
	bool startDelayed {false};
	bool startPaused {false};
	bool startConditional {false};
	bool startCondActiveJob {true};
	bool startCondTime {false};
	int startDelayedMinute {15};
	int startCondTimeStart {1};
	int startCondTimeEnd {6};
	int startCondActiveJobCount {1};
};

class JobCratePlotStartRule : public JobStartRule {
public:
	JobCratePlotStartRule();
	bool drawItemWidget() override;
	bool evaluate() override;
	JobCratePlotStartRuleParam param;
	std::chrono::time_point<std::chrono::system_clock> creationTime;
};

class JobCreatePlotFinishRuleParam {
public:
	JobCreatePlotFinishRuleParam(){};
	bool repeatJob {true};
	bool repeatIndefinite {false};
	bool execProg {false};
	std::filesystem::path progToExec;
	std::filesystem::path progWorkingDir;
	bool execProgOnRepeat {true};
	int repeatCount {1};
};

class JobCreatePlotFinishRule : public JobFinishRule {
public:
	bool drawItemWidget() override;
	JobCreatePlotFinishRuleParam param;
};

class CreatePlotContext {
public:
	std::shared_ptr<Job> job;
	std::shared_ptr<JobTaskItem> getCurrentTask();
	std::shared_ptr<JobTaskItem> pushTask(std::string name, uint32_t totalWorkItem = 0);
	std::shared_ptr<JobTaskItem> popTask(bool finish=true);
protected:
	std::stack<std::shared_ptr<JobTaskItem>> tasks;
};

class JobCreatePlotParam {
public:
	JobCreatePlotParam(){};
	std::filesystem::path destPath;
	std::filesystem::path tempPath;
	std::filesystem::path temp2Path;
	std::string poolKey;
	std::string farmKey;
	uint8_t ksize {32};
	uint32_t buckets {128};
	uint32_t stripes {65536};
	uint8_t threads {2};
	uint32_t buffer {4608};
	bool bitfield {true};
	JobCratePlotStartRuleParam startRuleParam;
	JobCreatePlotFinishRuleParam finishRuleParam;
	void loadDefault();
	bool isValid(std::string* errMsg = nullptr) const;
};

class WidgetCreatePlotStartRule : public Widget<JobCratePlotStartRuleParam*> {
public:
	bool draw() override;
};

class WidgetCreatePlotFinishRule : public Widget<JobCreatePlotFinishRuleParam*> {
public:
	bool draw() override;
};

class WidgetCreatePlot : public Widget<JobCreatePlotParam*> {
public:
	WidgetCreatePlot();
	bool draw() override;
	virtual void setData(JobCreatePlotParam* param) override;
protected:
	WidgetCreatePlotStartRule startRuleWidget;
	WidgetCreatePlotFinishRule finishRuleWidget;
};

class JobCreatePlot : public Job {
public:
	JobCreatePlot(std::string title);;
	JobCreatePlot(std::string title,const JobCreatePlotParam& param);
	static JobCreatePlotParam* drawUI();
	static int jobIdCounter;

	bool isRunning() const override;
	bool isFinished() const override;
	bool isPaused() const override;
	bool start() override;
	bool pause() override;
	bool cancel() override;
	float getProgress() override;
	JobRule& getStartRule() override;
	JobRule& getFinishRule() override;
	void drawItemWidget() override;
	void drawStatusWidget() override;

	std::shared_ptr<JobEvent> startEvent;
	std::shared_ptr<JobEvent> finishEvent;
	std::shared_ptr<JobEvent> phase1FinishEvent;
	std::shared_ptr<JobEvent> phase2FinishEvent;
	std::shared_ptr<JobEvent> phase3FinishEvent;
	std::shared_ptr<JobEvent> phase4FinishEvent;

protected:
	void init();
	bool preStartCheck(std::vector<std::string>& errs);
	bool paused {false};
	bool running {false};
	bool finished {false};
	float progress {0.0f};
	JobCreatePlotParam param;
	JobCratePlotStartRule startRule;
	JobCreatePlotFinishRule finishRule;
	WidgetCreatePlot jobEditor;	
};

#endif
