#ifndef _JOB_CHECK_PLOT_H_
#define _JOB_CHECK_PLOT_H_
#include "Job.hpp"
#include "JobRule.h"
#include <filesystem>
#include "ImFrame.h"

class JobCheckPlotParam {
public:
	JobCheckPlotParam();;
	JobCheckPlotParam(const JobCheckPlotParam& rhs);
	std::vector<std::string> paths;
	std::vector<std::pair<std::string, bool>> watchDirs;
	size_t iteration {100};
	bool drawEditor();
	std::vector<ImFrame::Filter> pickPlotFileFilter;
};

class JobCheckPlot : public Job {
public:
	static int jobIdCounter;
	JobCheckPlot(std::string title, std::string originalTitle = "");
	JobCheckPlot(std::string title, std::string originalTitle, JobCheckPlotParam& param);
	JobCheckPlot(std::string title,
		std::string originalTitle,
		JobCheckPlotParam& param,
		JobStartRuleParam& startRuleParam,
		JobFinishRuleParam& finishRuleParam
	);
	virtual ~JobCheckPlot(){};
	JobRule* getStartRule() override;
	JobRule* getFinishRule() override;
	bool drawEditor() override;
	bool drawItemWidget() override;
	bool drawStatusWidget() override;
	bool relaunchAfterFinish() override;
	std::shared_ptr<Job> relaunch() override;
protected:
	void initActivity() override;
	JobStartRule startRule;
	JobFinishRule finishRule;
	JobCheckPlotParam param;
};

class JobCheckPlotFactory : public JobFactory {
public:
	std::string getName() override;
	std::shared_ptr<Job> create(std::string jobName) override;
	bool drawEditor() override;
protected:
	JobCheckPlotParam param;
	JobStartRuleParam startRuleParam;
	JobFinishRuleParam finishRuleParam;
};

extern FactoryRegistration<JobCheckPlotFactory> JobCheckPlotFactoryRegistration;

#endif
