#ifndef CHIAGEN_JOB_CREATEPLOT_REF_H
#define CHIAGEN_JOB_CREATEPLOT_REF_H

#include "JobCreatePlot.h"
#include "gui.hpp"
#include <filesystem>

class JobCreatePlotRefParam {
public:
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
	void loadDefault();
	bool isValid(std::vector<std::string>& errs) const;
	virtual bool drawEditor();
	bool updateDerivedParams(std::vector<std::string>& errs);

	std::array<uint8_t, 32> plot_id = {};
	std::vector<uint8_t> memo_data;
	std::wstring plot_name;
	std::string id;
	std::string memo;
	std::wstring filename;
	std::wstring destPathStr;
	std::wstring tempPathStr;
	std::wstring temp2PathStr;
	std::filesystem::path destFile;
protected:
};

class JobCreatePlotRef : public JobCreatePlot {
public:
	JobCreatePlotRef(std::string title);
	JobCreatePlotRef(std::string title, JobCreatePlotRefParam& param);
	JobCreatePlotRef(std::string title,
		JobCreatePlotRefParam& param,
		JobCratePlotStartRuleParam& startRuleParam,
		JobCreatePlotFinishRuleParam& finishRuleParam
	);
	virtual bool drawEditor() override;
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
	JobCratePlotStartRuleParam startRuleParam;
	JobCreatePlotFinishRuleParam finishRuleParam;
};

extern FactoryRegistration<JobCreatePlotRefFactory> JobCreatePlotRefFactoryRegistration;

#endif