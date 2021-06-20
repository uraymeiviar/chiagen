#ifndef CHIAGEN_JOB_CREATEPLOT_MAX_H
#define CHIAGEN_JOB_CREATEPLOT_MAX_H
#include "JobCreatePlot.h"
#include "gui.hpp"
#include <filesystem>

class JobCreatePlotMaxParam {
public:
	JobCreatePlotMaxParam();
	std::filesystem::path destPath;
	std::filesystem::path tempPath;
	std::filesystem::path temp2Path;
	std::string poolKey;
	std::string farmKey;
	std::string puzzleHash;
	
	uint8_t threads {2};
	uint32_t buckets {256};
	size_t readChunkSize {65536};
	void loadDefault();
	void loadPreset();
	bool isValid(std::vector<std::string>& errs) const;
	bool updateDerivedParams(std::vector<std::string>& err);
	virtual bool drawEditor();

	std::array<uint8_t, 32> plot_id = {};
	std::vector<uint8_t> memo_data;
	std::wstring plot_name;
	std::wstring filename;
	std::wstring destPathStr;
	std::wstring tempPathStr;
	std::wstring temp2PathStr;
	std::filesystem::path destFileTempPath;
	std::string id;
	std::filesystem::path destFile;
};

class JobCreatePlotMax : public JobCreatePlot {
public:
	JobCreatePlotMax(std::string title, std::string originalTitle = "");
	JobCreatePlotMax(std::string title, std::string originalTitle, JobCreatePlotMaxParam& param);
	JobCreatePlotMax(std::string title,
		std::string originalTitle,
		JobCreatePlotMaxParam& param,
		JobStartRuleParam& startRuleParam,
		JobFinishRuleParam& finishRuleParam
	);
	virtual bool drawEditor() override;
	virtual std::shared_ptr<Job> relaunch() override;
protected:
	virtual void initActivity() override;
	JobCreatePlotMaxParam param;
};

class JobCreatePlotMaxFactory : public JobFactory {
public:
	std::string getName() override;
	std::shared_ptr<Job> create(std::string jobName) override;
	bool drawEditor() override;
protected:
	JobCreatePlotMaxParam param;
	JobStartRuleParam startRuleParam;
	JobFinishRuleParam finishRuleParam;
};

extern FactoryRegistration<JobCreatePlotMaxFactory> JobCreatePlotMaxFactoryRegistration;
 
#endif