#ifndef CHIAGEN_JOB_CREATEPLOT_REF_H
#define CHIAGEN_JOB_CREATEPLOT_REF_H

#include "JobCreatePlot.h"
#include "gui.hpp"
#include <filesystem>

class JobCreatePlotRefParam {
public:
	JobCreatePlotRefParam();
	JobCreatePlotRefParam(const JobCreatePlotRefParam& rhs);
	std::string destPath;
	std::string tempPath;
	std::string temp2Path;
	std::string poolKey;
	std::string farmKey;
	std::string puzzleHash;
	
	int ksize {32};
	int buckets {128};
	int stripes {65536};
	int threads {2};
	int buffer {4608};
	bool bitfield {true};
	void loadDefault();
	void loadPreset();
	bool isValid(std::vector<std::string>& errs) const;
	virtual bool drawEditor();
	bool updateDerivedParams(std::vector<std::string>& errs);

	std::array<uint8_t, 32> plot_id = {};
	std::vector<uint8_t> memo_data;
	std::string plot_name;
	std::string id;
	std::string memo;
	std::string filename;
	std::filesystem::path destFile;
protected:
};

class JobCreatePlotRef : public JobCreatePlot {
public:
	JobCreatePlotRef(std::string title, std::string originalTitle = "");
	JobCreatePlotRef(std::string title, std::string originalTitle, JobCreatePlotRefParam& param);
	JobCreatePlotRef(std::string title,
		std::string originalTitle,
		JobCreatePlotRefParam& param,
		JobStartRuleParam& startRuleParam,
		JobFinishRuleParam& finishRuleParam
	);
	virtual bool drawEditor() override;
	virtual std::shared_ptr<Job> relaunch() override;
protected:
	JobCreatePlotRefParam param;
	virtual void initActivity() override;
};

class JobCreatePlotRefFactory : public JobFactory {
public:
	std::string getName() override;
	std::shared_ptr<Job> create(std::string jobName) override;
	bool drawEditor() override;
protected:
	JobCreatePlotRefParam param;
	JobStartRuleParam startRuleParam;
	JobFinishRuleParam finishRuleParam;
};

extern FactoryRegistration<JobCreatePlotRefFactory> JobCreatePlotRefFactoryRegistration;

#endif