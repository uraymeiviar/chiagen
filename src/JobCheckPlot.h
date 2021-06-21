#ifndef _JOB_CHECK_PLOT_H_
#define _JOB_CHECK_PLOT_H_
#include "Job.hpp"
#include "JobRule.h"
#include <filesystem>
#include "ImFrame.h"
#include "chiapos/prover_disk.hpp"

class JobCheckPlotParam {
public:
	JobCheckPlotParam();
	JobCheckPlotParam(const JobCheckPlotParam& rhs);
	std::vector<std::wstring> paths;
	std::vector<std::pair<std::wstring, bool>> watchDirs;
	int iteration {50};
	bool drawEditor();
	std::vector<ImFrame::Filter> pickPlotFileFilter;
protected:
	bool uiSelectedIsDir {true};
	std::filesystem::path uiSelectedPath;
	std::wstring uiDelPath;
	std::wstring uiDelDirPath;
	std::string uiWatchDir;
	std::string uiWatchFile;
	std::wstring uiDelSelectedFile;
	std::vector<std::wstring> uiSelectedFiles;
	bool uiRecursive;
	int uiActiveTab;
};

class JobCheckPlotIterationResult {
public:
	std::string challenge;
	std::string proof;
};

class JobCheckPlotResult {
public:
	JobCheckPlotResult(std::wstring file, size_t iter):
		filePath(file), iter(iter), prover(file){
		this->prover.GetId(this->id_bytes);
		this->kSize = prover.GetSize();
		this->id = HexStr(id_bytes,32);
	}
	std::vector<std::shared_ptr<JobCheckPlotIterationResult>> success;
	std::vector<std::shared_ptr<JobCheckPlotIterationResult>> fails;
	std::wstring filePath;
	int kSize{32};
	size_t iter{50};
	std::string id;
	uint8_t id_bytes[32];
	DiskProver prover;
	size_t iterProgress {0};
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
	std::vector<std::shared_ptr<JobCheckPlotResult>> results;
protected:
	void initActivity() override;
	JobStartRule startRule;
	JobFinishRule finishRule;
	JobCheckPlotParam param;
	std::shared_ptr<JobEvent> startEvent;
	std::shared_ptr<JobEvent> finishEvent;
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
