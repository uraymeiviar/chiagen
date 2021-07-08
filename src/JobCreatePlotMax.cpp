#include "JobCreatePlotMax.h"
#include "imgui.h"
#include "ImFrame.h"
#include "chiapos/pos_constants.hpp"
#include "chiapos/entry_sizes.hpp"
#include "Imgui/misc/cpp/imgui_stdlib.h"
#include <thread>
#include "Keygen.hpp"
#include "util.hpp"
#include "main.hpp"

#include "madmax/phase1.hpp"
#include "madmax/phase2.hpp"
#include "madmax/phase3.hpp"
#include "madmax/phase4.hpp"
#include "madmax/chia.h"

FactoryRegistration<JobCreatePlotMaxFactory> JobCreatePlotMaxFactoryRegistration;

JobCreatePlotMaxParam::JobCreatePlotMaxParam()
{
	this->threads = std::thread::hardware_concurrency()/2;
	if (this->threads < 2) {
		this->threads = 2;
	}
	this->loadPreset();
}

void JobCreatePlotMaxParam::loadDefault()
{
	this->readChunkSize = 65536;
	this->threads = std::thread::hardware_concurrency()/2;
	if (this->threads < 2) {
		this->threads = 2;
	}
	this->temp2Path = "";
}

void JobCreatePlotMaxParam::loadPreset()
{
	MainApp::settings.load();
	this->buckets = MainApp::settings.buckets;
	this->threads = MainApp::settings.threads;
	this->temp2Path = MainApp::settings.tempDir2;
	this->tempPath = MainApp::settings.tempDir;
	this->destPath = MainApp::settings.finalDir;
	this->poolKey = MainApp::settings.poolKey;
	this->farmKey = MainApp::settings.farmKey;
	this->puzzleHash = MainApp::settings.puzzleHash;
}

bool JobCreatePlotMaxParam::isValid(std::vector<std::string>& errs) const
{
	bool result = true;
	if (this->threads < 1) {
		errs.push_back("invalid thread count");
		result = false;
	}

	if (this->poolKey.empty() && this->puzzleHash.empty()) {
		errs.push_back("pool public key or puzzle hash must be specified");
		result = false;
	}
	else {
		if(!this->puzzleHash.empty()){
			if (this->puzzleHash.length() < 2*32) {
				errs.push_back("puzzle hash must be atleast 32 bytes (64 hex characters)");
				result = false;
			}
		} else if (this->poolKey.length() < 2*48) {
			errs.push_back("pool public key must be 48 bytes (96 hex characters)");
			result = false;
		}
	}

	if (this->farmKey.empty()) {
		errs.push_back("farm public key must be specified");
		result = false;
	}
	else {
		if (this->farmKey.length() < 2*48) {
			errs.push_back("farm public key must be 48 bytes (96 hex characters)");
			result = false;
		}
	}
	return result;
}

bool JobCreatePlotMaxParam::updateDerivedParams(std::vector<std::string>& err)
{
	bool result = true;
	std::random_device r;
	std::default_random_engine randomEngine(r());
	std::uniform_int_distribution<int> uniformDist(0, UCHAR_MAX);
	std::vector<uint8_t> sk_data(32);
	std::generate(sk_data.begin(), sk_data.end(), [&uniformDist, &randomEngine] () {
		return uniformDist(randomEngine);
	});

	PrivateKey sk = KeyGen(Bytes(sk_data));
	err.push_back("sk        = " + hexStr(sk.Serialize()));

	G1Element local_pk = master_sk_to_local_sk(sk).GetG1Element();
	err.push_back("local pk  = " + hexStr(local_pk.Serialize()));

	std::vector<uint8_t> farmer_key_data(48);
	HexToBytes(this->farmKey,farmer_key_data.data());
	G1Element farmer_pk = G1Element::FromByteVector(farmer_key_data);
	err.push_back("farmer pk = " + hexStr(farmer_pk.Serialize()));

	G1Element plot_pk = local_pk + farmer_pk;
	err.push_back("plot pk   = " + hexStr(plot_pk.Serialize()));

	std::vector<uint8_t> plot_key_bytes = plot_pk.Serialize();
	std::vector<uint8_t> farm_key_bytes = farmer_pk.Serialize();
	std::vector<uint8_t> mstr_key_bytes = sk.Serialize();
			
	std::vector<uint8_t> plid_key_bytes;
	if (!this->puzzleHash.empty()) {
		std::vector<uint8_t> pzhs_key_bytes(32);
		if (!this->puzzleHash.empty()) {		
			HexToBytes(this->puzzleHash,pzhs_key_bytes.data());
			err.push_back("puzzle hash = " + hexStr(pzhs_key_bytes));
		}

		plid_key_bytes.insert(plid_key_bytes.end(), pzhs_key_bytes.begin(), pzhs_key_bytes.end());
		plid_key_bytes.insert(plid_key_bytes.end(), plot_key_bytes.begin(), plot_key_bytes.end());

		this->memo_data.insert(this->memo_data.end(), pzhs_key_bytes.begin(), pzhs_key_bytes.end());
		this->memo_data.insert(this->memo_data.end(), farm_key_bytes.begin(), farm_key_bytes.end());
		this->memo_data.insert(this->memo_data.end(), mstr_key_bytes.begin(), mstr_key_bytes.end());	
	}
	else {
		std::vector<uint8_t> pool_key_data(48);
		HexToBytes(this->poolKey,pool_key_data.data());
		G1Element pool_pk = G1Element::FromByteVector(pool_key_data);
		err.push_back("pool pk   = " + hexStr(pool_pk.Serialize()));

		std::vector<uint8_t> pool_key_bytes = pool_pk.Serialize();

		plid_key_bytes.insert(plid_key_bytes.end(), pool_key_bytes.begin(), pool_key_bytes.end());
		plid_key_bytes.insert(plid_key_bytes.end(), plot_key_bytes.begin(), plot_key_bytes.end());

		this->memo_data.insert(this->memo_data.end(), pool_key_bytes.begin(), pool_key_bytes.end());
		this->memo_data.insert(this->memo_data.end(), farm_key_bytes.begin(), farm_key_bytes.end());
		this->memo_data.insert(this->memo_data.end(), mstr_key_bytes.begin(), mstr_key_bytes.end());
	}

	Hash256(this->plot_id.data(),plid_key_bytes.data(),plid_key_bytes.size());
	id = hexStr(std::vector<uint8_t>(this->plot_id.begin(), this->plot_id.end()));
	err.push_back("plot id   = " + id);

	time_t t = std::time(nullptr);
	std::tm tm = *std::localtime(&t);
	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y-%m-%d-%H-%M");
	std::string timestr = oss.str();

	this->plot_name = "plot-k32-"+timestr+"-"+this->id;
	this->filename = this->plot_name+".plot";


	if (this->destPath.empty()) {
		this->destPath = std::filesystem::current_path().string();
	}

	this->destFile = this->destPath +"/"+ this->filename;
	if (!this->destPath.empty()) {
		if (this->destPath[this->destPath.length() - 1] != '/' ||
			this->destPath[this->destPath.length() - 1] != '\\') {
			this->destPath += "/";
		}
	}

	if (std::filesystem::exists(this->destPath)) {
		if (std::filesystem::is_regular_file(this->destPath)) {
			this->destFile = (std::filesystem::path(this->destPath).parent_path() / this->filename).string();
		}
	}
	else {
		if (std::filesystem::create_directory(destPath)) {
			this->destFile = (std::filesystem::path(this->destPath).parent_path() / this->filename).string();
		}
		else {
			err.push_back("!! unable to create directory " + destPath);
			result = false;
		}
	}
	this->destFileTempPath = std::filesystem::path(this->destPath) / (this->plot_name+".tmp");

	if(std::filesystem::exists(this->destFile)) {
		err.push_back("!! plot file already exists " + this->destFile);
		result = false;
	}

	if (this->tempPath.empty()) {
		this->tempPath = this->destPath;
	}
	else if (!this->tempPath.empty()) {
		if (this->tempPath[this->tempPath.length() - 1] != '/' ||
			this->tempPath[this->tempPath.length() - 1] != '\\') {
			this->tempPath += "/";
		}
	}

	if (std::filesystem::exists(tempPath)) {
		if (std::filesystem::is_regular_file(tempPath)) {
			tempPath = std::filesystem::path(tempPath).parent_path().string();
		}
	}
	else {
		if (std::filesystem::create_directory(tempPath)) {
		}
		else {
			err.push_back("!! unable to create temp directory " + tempPath);
			result = false;
		}
	}

	if (this->temp2Path.empty()) {
		this->temp2Path = this->tempPath;		
	}
	else if (!this->temp2Path.empty()) {
		if (this->temp2Path[this->temp2Path.length() - 1] != '/' ||
			this->temp2Path[this->temp2Path.length() - 1] != '\\') {
			this->temp2Path += "/";
		}
	}

	if (std::filesystem::exists(this->temp2Path)) {
		if (std::filesystem::is_regular_file(this->temp2Path)) {
			this->temp2Path = std::filesystem::path(this->temp2Path).parent_path().string();
		}
	}
	else {
		if (std::filesystem::create_directory(this->temp2Path)) {
		}
		else {
			err.push_back("!! unable to create temp2 directory " + this->temp2Path);
			result = false;
		}
	}

	if (this->threads < 2) {
		this->threads = 2;
	}
	return result;
}

bool JobCreatePlotMaxParam::drawEditor()
{
	bool result = false;
	float fieldWidth = ImGui::GetWindowContentRegionWidth();

	ImGui::PushItemWidth(90.0f);
	ImGui::Text("Pool Key");
	ImGui::PopItemWidth();
	ImGui::SameLine(90.0f);
	ImGui::PushItemWidth(fieldWidth-(90.0f + 55.0f));
	result |= ImGui::InputText("##poolkey", &this->poolKey,ImGuiInputTextFlags_CharsHexadecimal);
	if (ImGui::IsItemHovered() && !this->poolKey.empty()) {
		ImGui::BeginTooltip();
		tooltiipText(this->poolKey.c_str());
		ImGui::EndTooltip();
	}
	ImGui::PopItemWidth();
	ImGui::SameLine();
	ImGui::PushItemWidth(55.0f);
	if (ImGui::Button("Paste##pool")) {
		this->poolKey = ImGui::GetClipboardText();
		result |= true;
	}
	ImGui::PopItemWidth();

	ImGui::PushItemWidth(90.0f);
	ImGui::Text("PuzzleHash");
	ImGui::PopItemWidth();
	ImGui::SameLine(90.0f);
	ImGui::PushItemWidth(fieldWidth-(90.0f + 55.0f));
	result |= ImGui::InputText("##puzzleHash", &this->puzzleHash,ImGuiInputTextFlags_CharsHexadecimal);
	if (ImGui::IsItemHovered() && !this->puzzleHash.empty()) {
		ImGui::BeginTooltip();
		tooltiipText(this->puzzleHash.c_str());
		ImGui::EndTooltip();
	}
	ImGui::PopItemWidth();
	ImGui::SameLine();
	ImGui::PushItemWidth(55.0f);
	if (ImGui::Button("Paste##puzzleHash")) {
		this->puzzleHash = strFilterHexStr(ImGui::GetClipboardText());
		result |= true;
	}
	ImGui::PopItemWidth();

	ImGui::PushItemWidth(90.0f);
	ImGui::Text("Farm Key");
	ImGui::PopItemWidth();
	ImGui::SameLine(90.0f);
	ImGui::PushItemWidth(fieldWidth-(90.0f + 55.0f));
	ImGui::InputText("##farmkey", &this->farmKey,ImGuiInputTextFlags_CharsHexadecimal);
	if (ImGui::IsItemHovered() && !this->farmKey.empty()) {
		ImGui::BeginTooltip();
		tooltiipText(this->farmKey.c_str());
		ImGui::EndTooltip();
	}
	ImGui::PopItemWidth();
	ImGui::SameLine();
	ImGui::PushItemWidth(55.0f);
	if (ImGui::Button("Paste##farm")) {
		this->farmKey = ImGui::GetClipboardText();
		result |= true;
	}
	ImGui::PopItemWidth();

	ImGui::PushItemWidth(90.0f);
	ImGui::Text("Temp Dir");
	ImGui::PopItemWidth();
	ImGui::SameLine(90.0f);
	ImGui::PushItemWidth(fieldWidth-(90.0f + 105.0f));
	result |= ImGui::InputText("##tempDir", &this->tempPath);
	if (ImGui::IsItemHovered() && !this->tempPath.empty()) {
		ImGui::BeginTooltip();
		tooltiipText(this->tempPath.c_str());
		ImGui::EndTooltip();
	}
	ImGui::PopItemWidth();
		
	ImGui::PushItemWidth(55.0f);
	ImGui::SameLine();		
	if (ImGui::Button("Paste##tempDir")) {
		std::string pastedtempDir = ImGui::GetClipboardText();
		if (!pastedtempDir.empty()) {
			this->tempPath = pastedtempDir;
		}		
		result |= true;
	}
	ImGui::PopItemWidth();
	ImGui::PushItemWidth(55.0f);
	ImGui::SameLine();
	if (ImGui::Button("Select##tempDir")) {
		std::optional<std::filesystem::path> dirPath = ImFrame::PickFolderDialog();
		if (dirPath) {
			this->tempPath = dirPath->string();
		}
		result |= true;
	}
	ImGui::PopItemWidth();

	ImGui::PushItemWidth(90.0f);
	ImGui::Text("Dest Dir");
	ImGui::PopItemWidth();
	ImGui::SameLine(90.0f);
	ImGui::PushItemWidth(fieldWidth-(90.0f + 105.0f));
	result |= ImGui::InputText("##destDir", &this->destPath);
	if (ImGui::IsItemHovered() && !this->destPath.empty()) {
		ImGui::BeginTooltip();
		tooltiipText(this->destPath.c_str());
		ImGui::EndTooltip();
	}
	ImGui::PopItemWidth();
	ImGui::PushItemWidth(55.0f);
	ImGui::SameLine();
	if (ImGui::Button("Paste##destDir")) {
		std::string pasted = ImGui::GetClipboardText();
		if(!pasted.empty()){
			this->destPath = pasted;
		}
		result |= true;
	}
	ImGui::PopItemWidth();
	ImGui::PushItemWidth(55.0f);
	ImGui::SameLine();
	if (ImGui::Button("Select##destDir")) {
		std::optional<std::filesystem::path> dirPath = ImFrame::PickFolderDialog();
		if (dirPath) {
			this->destPath = dirPath.value().string();
		}
		result |= true;
	}
	ImGui::PopItemWidth();

	if (ImGui::CollapsingHeader("Advanced")) {
		ImGui::Indent(20.0f);
		fieldWidth = ImGui::GetWindowContentRegionWidth();
			
		ImGui::Text("Temp Dir2");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-225.0f);
		result |= ImGui::InputText("##tempDir2", &this->temp2Path);
		if (ImGui::IsItemHovered() && !this->temp2Path.empty()) {
			ImGui::BeginTooltip();
			tooltiipText(this->temp2Path.c_str());
			ImGui::EndTooltip();
			result |= true;
		}
		ImGui::PopItemWidth();
		ImGui::SameLine();
		ImGui::PushItemWidth(50.0f);
		if (ImGui::Button("Paste##tempDir2")) {
			std::string pastedTempDir2 = ImGui::GetClipboardText();
			if(!pastedTempDir2.empty()){
				this->temp2Path = pastedTempDir2;
			}
			result |= true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Select##tempDir2")) {
			std::optional<std::filesystem::path> dirPath = ImFrame::PickFolderDialog();
			if (dirPath) {
				this->temp2Path = dirPath.value().string();
			}
			result |= true;
		}
		ImGui::PopItemWidth();

		ImGui::Text("Threads");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-130.0f);
		if (ImGui::InputInt("##threads", &this->threads, 1, 2)) {
			if (this->threads < 1) {
				this->threads = 1;
			}
			result |= true;
		}
		ImGui::PopItemWidth();

		ImGui::Text("Buckets");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-130.0f);
		if (ImGui::InputInt("##buckets", &this->buckets, 1, 8)) {
			if (this->buckets < 16) {
				this->buckets = 16;
			}
			if (this->buckets > 256) {
				this->buckets = 256;
			}
			result |= true;
		}

		ImGui::Unindent(20.0f);
	}

	return result;
}

JobCreatePlotMax::JobCreatePlotMax(std::string title, std::string originalTitle)
	: JobCreatePlot(title, originalTitle)
{

}

JobCreatePlotMax::JobCreatePlotMax(std::string title, 
	std::string originalTitle, 
	JobCreatePlotMaxParam& param)
	:JobCreatePlot(title, originalTitle, JobStartRuleParam(), JobFinishRuleParam()),
	 param(param)
{

}

JobCreatePlotMax::JobCreatePlotMax(std::string title,
	std::string originalTitle, 
	JobCreatePlotMaxParam& param, 
	JobStartRuleParam& startRuleParam, 
	JobFinishRuleParam& finishRuleParam)	:
	JobCreatePlot(title, originalTitle, startRuleParam, finishRuleParam),
	param(param)
{

}

bool JobCreatePlotMax::drawEditor()
{
	bool result = this->param.drawEditor();
	if (ImGui::CollapsingHeader("Start Rule")) {
		ImGui::Indent(20.0f);
		result |= this->startRule.drawEditor();
		ImGui::Unindent(20.0f);
	}	

	if (ImGui::CollapsingHeader("Finish Rule")) {
		ImGui::Indent(20.0f);
		result |= this->finishRule.drawEditor();
		ImGui::Unindent(20.0f);
	}

	std::vector<std::string> errMsg;
	result = this->param.isValid(errMsg);

	if (!errMsg.empty()) {
		for (auto err : errMsg) {
			ImGui::Text(err.c_str());
		}		
	}
	return result;
}

std::shared_ptr<Job> JobCreatePlotMax::relaunch()
{
	JobStartRule* startRule = dynamic_cast<JobStartRule*>(this->getStartRule());
	JobFinishRule* finishRule = dynamic_cast<JobFinishRule*>(this->getFinishRule());
	JobStartRuleParam& startParam = startRule->getRelaunchParam();
	JobFinishRuleParam& finishParam = finishRule->getRelaunchParam();
	if (!finishParam.repeatIndefinite && finishParam.repeatCount <= 0) {
		startParam.startPaused = true;
	}
	auto newJob = std::make_shared<JobCreatePlotMax>(
		this->getOriginalTitle()+"#"+systemClockToStr(std::chrono::system_clock::now()),
		this->getOriginalTitle(),
		this->param, 
		startParam,
		finishParam
	);
	return newJob;
}

void JobCreatePlotMax::initActivity()
{
	JobCreatePlot::initActivity();
	if (this->activity) {
		this->activity->mainRoutine = [=](JobActivityState*) {
			std::vector<std::string> err;
			bool result = param.updateDerivedParams(err);
			result &= param.isValid(err);

			for (auto msg : err) {
				std::cout << msg << std::endl;
			}

			if (result) {
				const auto total_begin = get_wall_time_micros();
				mad::phase1::input_t params;
				params.id = param.plot_id;
				params.memo = param.memo_data;
				params.plot_name = s2ws(param.plot_name);
				params.log_num_buckets = int(log2(param.buckets));
				params.tempDir = s2ws(param.tempPath);
				params.tempDir2 = s2ws(param.temp2Path);
				params.num_threads = param.threads;

				mad::DiskPlotterContext context;
				context.job = this->shared_from_this();

				context.log("plotname   "+param.plot_name);
				context.log("target dir "+param.destPath);
				context.log("temp dir   "+param.tempPath);
				context.log("temp2 dir  "+param.temp2Path);
				context.log("threads    "+std::to_string(param.threads));

				if (context.job->activity) {
					//std::shared_ptr<JobCreatePlot> plottingJob = std::dynamic_pointer_cast<JobCreatePlot>(context.job);
					//plottingJob->startEvent->trigger(context.job);
					//context.job->activity->totalWorkItem = 100;
					//while (context.job->activity->completedWorkItem < context.job->activity->totalWorkItem) {
					//	context.job->activity->completedWorkItem++;
					//	std::this_thread::sleep_for(std::chrono::milliseconds(100));
					//}
					//plottingJob->finishEvent->trigger(context.job);

					std::shared_ptr<JobTaskItem> currentTask = context.getCurrentTask();
					context.pushTask("PlotCopy", currentTask);
					context.pushTask("Phase4", currentTask);
					context.pushTask("Phase3", currentTask);
					context.pushTask("Phase2", currentTask);
					context.pushTask("Phase1", currentTask);
	
					try {
						std::shared_ptr<JobCreatePlot> plottingJob = std::dynamic_pointer_cast<JobCreatePlot>(context.job);
						plottingJob->startEvent->trigger(context.job);

						context.getCurrentTask()->start();
						mad::phase1::output_t out_1;
						mad::phase1::Phase1 p1(&context);
						p1.compute(params, out_1);
						context.popTask();
						plottingJob->phase1FinishEvent->trigger(context.job);

						context.getCurrentTask()->start();
						mad::phase2::output_t out_2;
						mad::phase2::compute(context, out_1, out_2);	
						context.popTask();
						plottingJob->phase2FinishEvent->trigger(context.job);

						context.getCurrentTask()->start();
						mad::phase3::output_t out_3;
						mad::phase3::compute(context, out_2, out_3);
						context.popTask();
						plottingJob->phase3FinishEvent->trigger(context.job);

						context.getCurrentTask()->start();
						mad::phase4::output_t out_4;
						mad::phase4::compute(context, out_3, out_4);
						context.popTask();
						plottingJob->phase4FinishEvent->trigger(context.job);

						context.log("Total plot creation time was "
							+ std::to_string((get_wall_time_micros() - total_begin) / 1e6) + " sec");

						context.getCurrentTask()->start();
						if(param.tempPath != param.destPath)
						{
							context.log("Started copy to " + param.destFile);
							const auto total_begin = get_wall_time_micros();
							std::filesystem::copy(out_4.plot_file_name, param.destFile);
							_wremove(out_4.plot_file_name.c_str());
							const auto time = (get_wall_time_micros() - total_begin) / 1e6;	
							context.log("Move to " + param.destFile +" finished, took " + std::to_string(time) +" sec ");
						}
						context.popTask();
						plottingJob->finishEvent->trigger(context.job);
					}
					catch (...) {

					}
					
					//this->activity->waitUntilFinish();
				}
			}
		};
	}
}

std::string JobCreatePlotMaxFactory::getName()
{
	return "CreatePlot (MadMaxx\'s plotter)";
}

std::shared_ptr<Job> JobCreatePlotMaxFactory::create(std::string jobName)
{
	return std::make_shared<JobCreatePlotMax>(
		jobName, jobName, this->param, this->startRuleParam, this->finishRuleParam
	);
}

bool JobCreatePlotMaxFactory::drawEditor()
{
	bool result = this->param.drawEditor();
	if (ImGui::CollapsingHeader("Start Rule")) {
		ImGui::Indent(20.0f);
		result |= this->startRuleParam.drawEditor();
		ImGui::Unindent(20.0f);
	}

	if (ImGui::CollapsingHeader("Finish Rule")) {
		ImGui::Indent(20.0f);
		result |= this->finishRuleParam.drawEditor();
		ImGui::Unindent(20.0f);
	}

	std::vector<std::string> errMsg;
	result = this->param.isValid(errMsg);

	if (!errMsg.empty()) {
		for (auto err : errMsg) {
			ImGui::Text(err.c_str());
		}		
	}

	ImGui::Separator();
	
	float fieldWidth = ImGui::GetWindowContentRegionWidth();

	ImGui::Text("Job Name");
	ImGui::SameLine(90.0f);
	ImGui::PushItemWidth(fieldWidth-160.0f);
	std::string jobName = "createplot-"+std::to_string(JobCreatePlot::jobIdCounter);
	ImGui::InputText("##jobName",&jobName);
	ImGui::PopItemWidth();

	ImGui::SameLine();
	ImGui::PushItemWidth(50.0f);
	if(!result){
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
	}
	if (ImGui::Button("Add Job")) {
		if (result) {
			JobManager::getInstance().addJob(this->create(jobName));
			JobCreatePlot::jobIdCounter++;
		}
	}
	if (!result) {
		ImGui::PopStyleVar();
	}
	ImGui::PopItemWidth();


	return result;
}
