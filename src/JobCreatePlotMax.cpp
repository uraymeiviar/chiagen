#include "JobCreatePlotMax.h"
#include "imgui.h"
#include "ImFrame.h"
#include "chiapos/pos_constants.hpp"
#include "chiapos/entry_sizes.hpp"
#include "Imgui/misc/cpp/imgui_stdlib.h"
#include <thread>
#include "Keygen.hpp"
#include "util.hpp"

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

bool JobCreatePlotMaxParam::isValid(std::vector<std::string>& errs) const
{
	bool result = true;
	if (this->threads < 1) {
		errs.push_back("invalid thread count");
		result = false;
	}

	if (this->poolKey.empty()) {
		errs.push_back("pool public key must be specified");
		result = false;
	}
	else {
		if (this->poolKey.length() < 2*48) {
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

	std::vector<uint8_t> pool_key_data(48);
	HexToBytes(this->poolKey,pool_key_data.data());
	G1Element pool_pk = G1Element::FromByteVector(pool_key_data);
	err.push_back("pool pk   = " + hexStr(pool_pk.Serialize()));

	std::vector<uint8_t> pool_key_bytes = pool_pk.Serialize();
	std::vector<uint8_t> plot_key_bytes = plot_pk.Serialize();
	std::vector<uint8_t> farm_key_bytes = farmer_pk.Serialize();
	std::vector<uint8_t> mstr_key_bytes = sk.Serialize();
			
	std::vector<uint8_t> plid_key_bytes;
	plid_key_bytes.insert(plid_key_bytes.end(), pool_key_bytes.begin(), pool_key_bytes.end());
	plid_key_bytes.insert(plid_key_bytes.end(), plot_key_bytes.begin(), plot_key_bytes.end());

	Hash256(this->plot_id.data(),plid_key_bytes.data(),plid_key_bytes.size());
	id = hexStr(std::vector<uint8_t>(this->plot_id.begin(), this->plot_id.end()));
	err.push_back("plot id   = " + id);

	this->memo_data.insert(this->memo_data.end(), pool_key_bytes.begin(), pool_key_bytes.end());
	this->memo_data.insert(this->memo_data.end(), farm_key_bytes.begin(), farm_key_bytes.end());
	this->memo_data.insert(this->memo_data.end(), mstr_key_bytes.begin(), mstr_key_bytes.end());

	time_t t = std::time(nullptr);
	std::tm tm = *std::localtime(&t);
	std::wostringstream oss;
	oss << std::put_time(&tm, L"%Y-%m-%d-%H-%M");
	std::wstring timestr = oss.str();

	this->plot_name = L"plot-k32-"+timestr+L"-"+s2ws(this->id);
	this->filename = this->plot_name+L".plot";


	if (this->destPath.empty()) {
		this->destPath = std::filesystem::current_path();
	}

	this->destFile = this->destPath / this->filename;

	if (std::filesystem::exists(this->destPath)) {
		if (std::filesystem::is_regular_file(this->destPath)) {
			this->destFile = this->destPath.parent_path() / this->filename;
		}
	}
	else {
		if (std::filesystem::create_directory(destPath)) {
			this->destFile = this->destPath.parent_path() / this->filename;
		}
		else {
			err.push_back("!! unable to create directory " + destPath.string());
			result = false;
		}
	}
	this->destPathStr = this->destPath.wstring() + L"/";
	this->destFileTempPath = this->destPath / (this->plot_name+L".tmp");

	if(std::filesystem::exists(this->destFile)) {
		err.push_back("!! plot file already exists " + this->destFile.string());
		result = false;
	}

	if (this->tempPath.empty()) {
		this->tempPath = this->destPath;
	}

	if (std::filesystem::exists(tempPath)) {
		if (std::filesystem::is_regular_file(tempPath)) {
			tempPath = tempPath.parent_path();
		}
	}
	else {
		if (std::filesystem::create_directory(tempPath)) {
		}
		else {
			err.push_back("!! unable to create temp directory " + tempPath.string());
			result = false;
		}
	}
	this->tempPathStr = this->tempPath.wstring() + L"/";

	if (this->temp2Path.empty()) {
		this->temp2Path = this->tempPath;		
	}

	if (std::filesystem::exists(this->temp2Path)) {
		if (std::filesystem::is_regular_file(this->temp2Path)) {
			this->temp2Path =this-> temp2Path.parent_path();
		}
	}
	else {
		if (std::filesystem::create_directory(this->temp2Path)) {
		}
		else {
			err.push_back("!! unable to create temp2 directory " + this->temp2Path.string());
			result = false;
		}
	}

	this->temp2PathStr = this->temp2Path.wstring() + L"/";
	if (this->threads < 2) {
		this->threads = 2;
	}
	return result;
}

bool JobCreatePlotMaxParam::drawEditor()
{
	bool result = false;
	ImGui::PushID((const void*)this);
	float fieldWidth = ImGui::GetWindowContentRegionWidth();

	ImGui::PushItemWidth(80.0f);
	ImGui::Text("Pool Key");
	ImGui::PopItemWidth();
	ImGui::SameLine(80.0f);
	ImGui::PushItemWidth(fieldWidth-(80.0f + 55.0f));
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

	ImGui::PushItemWidth(80.0f);
	ImGui::Text("Farm Key");
	ImGui::PopItemWidth();
	ImGui::SameLine(80.0f);
	ImGui::PushItemWidth(fieldWidth-(80.0f + 55.0f));
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

	ImGui::PushItemWidth(80.0f);
	ImGui::Text("Temp Dir");
	ImGui::PopItemWidth();
	ImGui::SameLine(80.0f);
	static std::string tempDir;
	ImGui::PushItemWidth(fieldWidth-(80.0f + 105.0f));
	if (ImGui::InputText("##tempDir", &tempDir)) {
		this->tempPath = tempDir;
		result |= true;
	}
	if (ImGui::IsItemHovered() && !tempDir.empty()) {
		ImGui::BeginTooltip();
		tooltiipText(tempDir.c_str());
		ImGui::EndTooltip();
	}
	ImGui::PopItemWidth();
		
	ImGui::PushItemWidth(55.0f);
	ImGui::SameLine();		
	if (ImGui::Button("Paste##tempDir")) {
		tempDir = ImGui::GetClipboardText();
		this->tempPath = tempDir;
		result |= true;
	}
	ImGui::PopItemWidth();
	ImGui::PushItemWidth(55.0f);
	ImGui::SameLine();
	if (ImGui::Button("Select##tempDir")) {
		std::optional<std::filesystem::path> dirPath = ImFrame::PickFolderDialog();
		if (dirPath) {
			tempDir = dirPath->string();
			this->tempPath = tempDir;
		}
		result |= true;
	}
	ImGui::PopItemWidth();

	ImGui::PushItemWidth(80.0f);
	ImGui::Text("Dest Dir");
	ImGui::PopItemWidth();
	ImGui::SameLine(80.0f);
	static std::string destDir;
	ImGui::PushItemWidth(fieldWidth-(80.0f + 105.0f));
	if (ImGui::InputText("##destDir", &destDir)) {
		this->destPath = destDir;
		result |= true;
	}
	if (ImGui::IsItemHovered() && !destDir.empty()) {
		ImGui::BeginTooltip();
		tooltiipText(destDir.c_str());
		ImGui::EndTooltip();
	}
	ImGui::PopItemWidth();
	ImGui::PushItemWidth(55.0f);
	ImGui::SameLine();
	if (ImGui::Button("Paste##destDir")) {
		destDir = ImGui::GetClipboardText();
		this->destPath = destDir;
		result |= true;
	}
	ImGui::PopItemWidth();
	ImGui::PushItemWidth(55.0f);
	ImGui::SameLine();
	if (ImGui::Button("Select##destDir")) {
		std::optional<std::filesystem::path> dirPath = ImFrame::PickFolderDialog();
		if (dirPath) {
			destDir = dirPath.value().string();
			this->destPath = dirPath.value();
		}
		result |= true;
	}
	ImGui::PopItemWidth();

	if (ImGui::CollapsingHeader("Advanced")) {
		ImGui::Indent(20.0f);
		fieldWidth = ImGui::GetWindowContentRegionWidth();
			
		ImGui::Text("Temp Dir2");
		ImGui::SameLine(120.0f);
		static std::string tempDir2;
		ImGui::PushItemWidth(fieldWidth-225.0f);
		if (ImGui::InputText("##tempDir2", &tempDir2)) {
			this->temp2Path = tempDir2;
			result |= true;
		}
		if (ImGui::IsItemHovered() && !tempDir2.empty()) {
			ImGui::BeginTooltip();
			tooltiipText(tempDir2.c_str());
			ImGui::EndTooltip();
			result |= true;
		}
		ImGui::PopItemWidth();
		ImGui::SameLine();
		ImGui::PushItemWidth(50.0f);
		if (ImGui::Button("Paste##tempDir2")) {
			tempDir2 = ImGui::GetClipboardText();
			this->temp2Path = tempDir2;
			result |= true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Select##tempDir2")) {
			std::optional<std::filesystem::path> dirPath = ImFrame::PickFolderDialog();
			if (dirPath) {
				tempDir2 = dirPath.value().string();
				this->temp2Path = dirPath.value();
			}
			result |= true;
		}
		ImGui::PopItemWidth();

		ImGui::Text("Threads");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-130.0f);
		static int threadsInput = (int)this->threads;
		if (ImGui::InputInt("##threads", &threadsInput, 1, 2)) {
			this->threads = (uint8_t)threadsInput;
			if (this->threads < 1) {
				this->threads = 1;
			}
			result |= true;
		}
		ImGui::PopItemWidth();

		ImGui::Text("Buckets");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-130.0f);
		static int bucketInput = (int)this->buckets;
		if (ImGui::InputInt("##buckets", &bucketInput, 1, 8)) {
			this->buckets = (uint32_t)bucketInput;
			if (this->buckets < 16) {
				this->buckets = 16;
			}
			if (this->buckets > 256) {
				this->buckets = 256;
			}
			bucketInput= this->buckets;
			result |= true;
		}

		ImGui::Unindent(20.0f);
	}

	ImGui::PopID();
	return result;
}

JobCreatePlotMax::JobCreatePlotMax(std::string title): JobCreatePlot(title)
{

}

JobCreatePlotMax::JobCreatePlotMax(std::string title, JobCreatePlotMaxParam& param)
	:JobCreatePlot(title, JobCratePlotStartRuleParam(), JobCreatePlotFinishRuleParam()),
	 param(param)
{

}

JobCreatePlotMax::JobCreatePlotMax(std::string title,
	JobCreatePlotMaxParam& param, 
	JobCratePlotStartRuleParam& startRuleParam, 
	JobCreatePlotFinishRuleParam& finishRuleParam)	:
	JobCreatePlot(title, startRuleParam, finishRuleParam),
	param(param)
{

}

bool JobCreatePlotMax::drawEditor()
{
	ImGui::PushID((const void*)this);	
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
	ImGui::PopID();
	return result;
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
				params.plot_name = param.plot_name;
				params.log_num_buckets = int(log2(param.buckets));
				params.tempDir = param.tempPathStr;
				params.tempDir2 = param.temp2PathStr;
				params.num_threads = param.threads;

				mad::DiskPlotterContext context;
				context.job = this->shared_from_this();

				if (context.job->activity) {
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

						std::cout << "Total plot creation time was "
							<< (get_wall_time_micros() - total_begin) / 1e6 << " sec" << std::endl;

						context.getCurrentTask()->start();
						if(param.tempPathStr != param.destPathStr)
						{
							std::wcout << L"Started copy to " << param.destFile.wstring() << std::endl;
							const auto total_begin = get_wall_time_micros();
							std::filesystem::copy(out_4.plot_file_name, param.destFile);
							_wremove(out_4.plot_file_name.c_str());
							const auto time = (get_wall_time_micros() - total_begin) / 1e6;	
							std::wcout << L"Move to " << param.destFile.wstring() << L" finished, took " << time << L" sec " << std::endl;
						}
						context.popTask();
						plottingJob->finishEvent->trigger(context.job);
					}
					catch (...) {

					}
					this->activity->waitUntilFinish();
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
		jobName, this->param, this->startRuleParam, this->finishRuleParam
	);
}

bool JobCreatePlotMaxFactory::drawEditor()
{
	ImGui::PushID((const void*)this);	
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
	
	float fieldWidth = ImGui::GetWindowContentRegionWidth();

	ImGui::Text("Job Name");
	ImGui::SameLine(90.0f);
	ImGui::PushItemWidth(fieldWidth-(result?160.0f:90.0f));
	std::string jobName = "createplot-"+std::to_string(JobCreatePlot::jobIdCounter);
	ImGui::InputText("##jobName",&jobName);
	ImGui::PopItemWidth();
	if (result) {
		ImGui::SameLine();
		ImGui::PushItemWidth(50.0f);
		if (ImGui::Button("Add Job")) {
			JobManager::getInstance().addJob(this->create(jobName));
			JobCreatePlot::jobIdCounter++;
		}
		ImGui::PopItemWidth();
	}		

	ImGui::PopID();
	return result;
}
