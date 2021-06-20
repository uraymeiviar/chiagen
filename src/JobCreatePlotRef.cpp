#include "JobCreatePlotRef.h"
#include <stdint.h>
#include "imgui.h"
#include "ImFrame.h"
#include "chiapos/pos_constants.hpp"
#include "chiapos/entry_sizes.hpp"
#include "Imgui/misc/cpp/imgui_stdlib.h"
#include <chrono>

#include <vector>
#include <filesystem>
#include "Keygen.hpp"
#include "util.hpp"
#include "main.hpp"

#include "chiapos/picosha2.hpp"
#include "chiapos/plotter_disk.hpp"
#include "chiapos/prover_disk.hpp"
#include "chiapos/verifier.hpp"
#include "chiapos/thread_pool.hpp"

FactoryRegistration<JobCreatePlotRefFactory> JobCreatePlotRefFactoryRegistration;

JobCreatePlotRefParam::JobCreatePlotRefParam()
{
	this->loadPreset();
}

JobCreatePlotRefParam::JobCreatePlotRefParam(const JobCreatePlotRefParam& rhs)
{
	this->buckets   = rhs.buckets;
	this->stripes   = rhs.stripes;
	this->threads   = rhs.threads;
	this->buffer    = rhs.buffer;
	this->ksize     = rhs.ksize;
	this->temp2Path = rhs.temp2Path;
	this->tempPath  = rhs.tempPath;
	this->destPath  = rhs.destPath;
	this->bitfield  = rhs.bitfield;
	this->puzzleHash= rhs.puzzleHash;
}

void JobCreatePlotRefParam::loadDefault()
{
	this->buckets = 256;
	this->stripes = 65536;
	this->threads = 2;
	this->buffer = 4608;
	this->ksize = 32;
	this->temp2Path = "";
	this->bitfield = true;
}

void JobCreatePlotRefParam::loadPreset()
{
	MainApp::settings.load();
	this->buckets = MainApp::settings.buckets;
	this->stripes = MainApp::settings.stripes;
	this->threads = MainApp::settings.threads;
	this->buffer = MainApp::settings.buffer;
	this->ksize = MainApp::settings.ksize;
	this->temp2Path = MainApp::settings.tempDir2;
	this->tempPath = MainApp::settings.tempDir;
	this->destPath = MainApp::settings.finalDir;
	this->bitfield = MainApp::settings.bitfield;
	this->poolKey = MainApp::settings.poolKey;
	this->farmKey = MainApp::settings.farmKey;
	this->puzzleHash = MainApp::settings.puzzleHash;
}

bool JobCreatePlotRefParam::drawEditor()
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
		this->poolKey = strFilterHexStr(ImGui::GetClipboardText());
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
		this->farmKey = strFilterHexStr(ImGui::GetClipboardText());
		result |= true;
	}
	ImGui::PopItemWidth();

	ImGui::PushItemWidth(90.0f);
	ImGui::Text("Temp Dir");
	ImGui::PopItemWidth();
	ImGui::SameLine(90.0f);
	static std::string tempDir = this->tempPath.string();
	ImGui::PushItemWidth(fieldWidth-(90.0f + 105.0f));
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

	ImGui::PushItemWidth(90.0f);
	ImGui::Text("Dest Dir");
	ImGui::PopItemWidth();
	ImGui::SameLine(90.0f);
	static std::string destDir = this->destPath.string();
	ImGui::PushItemWidth(fieldWidth-(90.0f + 105.0f));
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
			result |= true;
		}		
	}
	ImGui::PopItemWidth();

	if (ImGui::CollapsingHeader("Advanced")) {
		ImGui::Indent(20.0f);
		fieldWidth = ImGui::GetWindowContentRegionWidth();

		ImGui::Text("Plot Size (k)");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-130.0f);
		static int kinput = (int)this->ksize;
		if (ImGui::InputInt("##k", &kinput, 1, 10)) {
			this->ksize = (uint8_t)kinput;
			if (this->ksize < 18) {
				this->ksize = 18;
			}
			if (this->ksize > 50) {
				this->ksize = 50;
			}
			kinput = (int)this->ksize;
			result |= true;
		}
		ImGui::PopItemWidth();
			
		ImGui::Text("Temp Dir2");
		ImGui::SameLine(120.0f);
		static std::string tempDir2 = this->temp2Path.string();
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

		ImGui::Text("Stripes");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-130.0f);
		static int stripesInput = (int)this->stripes;
		if (ImGui::InputInt("##stripes", &stripesInput, 1024, 4096)) {
			this->stripes = stripesInput;
			if (this->stripes < 1) {
				this->stripes = 1;
				stripesInput = this->stripes;
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

		ImGui::Text("Buffer (MB)");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-130.0f);
		static int bufferInput = (int)this->buffer;
		if (ImGui::InputInt("##buffer", &bufferInput, 1024, 2048)) {
			this->buffer = bufferInput;
			if (this->buffer < 16) {
				this->buffer = 16;
			}
			result |= true;
		}
		ImGui::PopItemWidth();

		ImGui::Text("Bitfield");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-130.0f);
		ImGui::Checkbox("##bitfeld", &this->bitfield);
		ImGui::PopItemWidth();

		if (ImGui::Button("Load Default")) {
			this->loadDefault();
			result |= true;
		}

		ImGui::Unindent(20.0f);
	}
	return result;
}

bool JobCreatePlotRefParam::updateDerivedParams(std::vector<std::string>& err)
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
	err.push_back("sk          = " + hexStr(sk.Serialize()));

	G1Element local_pk = master_sk_to_local_sk(sk).GetG1Element();
	err.push_back("local pk    = " + hexStr(local_pk.Serialize()));

	std::vector<uint8_t> farmer_key_data(48);
	HexToBytes(this->farmKey,farmer_key_data.data());
	G1Element farmer_pk = G1Element::FromByteVector(farmer_key_data);
	err.push_back("farmer pk   = " + hexStr(farmer_pk.Serialize()));

	G1Element plot_pk = local_pk + farmer_pk;
	err.push_back("plot pk     = " + hexStr(plot_pk.Serialize()));

	std::vector<uint8_t> pool_key_data(48);
	HexToBytes(this->poolKey,pool_key_data.data());
	G1Element pool_pk = G1Element::FromByteVector(pool_key_data);
	err.push_back("pool pk     = " + hexStr(pool_pk.Serialize()));

	std::vector<uint8_t> pzhs_key_bytes(32);
	if (!this->puzzleHash.empty()) {		
		HexToBytes(this->puzzleHash,pzhs_key_bytes.data());
		err.push_back("puzzle hash = " + hexStr(pool_pk.Serialize()));
	}

	std::vector<uint8_t> pool_key_bytes = pool_pk.Serialize();
	std::vector<uint8_t> plot_key_bytes = plot_pk.Serialize();
	std::vector<uint8_t> farm_key_bytes = farmer_pk.Serialize();
	std::vector<uint8_t> mstr_key_bytes = sk.Serialize();
			
	std::vector<uint8_t> plid_key_bytes;
	if (!this->puzzleHash.empty()) {
		plid_key_bytes.insert(plid_key_bytes.end(), pzhs_key_bytes.begin(), pzhs_key_bytes.end());
		plid_key_bytes.insert(plid_key_bytes.end(), plot_key_bytes.begin(), plot_key_bytes.end());
	}
	else {
		plid_key_bytes.insert(plid_key_bytes.end(), pool_key_bytes.begin(), pool_key_bytes.end());
		plid_key_bytes.insert(plid_key_bytes.end(), plot_key_bytes.begin(), plot_key_bytes.end());
	}

	if (id.empty()) {
		Hash256(this->plot_id.data(),plid_key_bytes.data(),plid_key_bytes.size());
	}
	else {
		id = Strip0x(id);
		if (id.size() != 64) {
			err.push_back("!! Invalid ID, should be 32 bytes (hex)");
			result = false;
		}
		HexToBytes(id, this->plot_id.data());
	}	
	id = hexStr(std::vector<uint8_t>(this->plot_id.begin(), this->plot_id.end()));
	err.push_back("plot id   = " + id);

	if (this->memo.empty()) {
		if (!this->puzzleHash.empty()) {
			this->memo_data.insert(this->memo_data.end(), pzhs_key_bytes.begin(), pzhs_key_bytes.end());
			this->memo_data.insert(this->memo_data.end(), farm_key_bytes.begin(), farm_key_bytes.end());
			this->memo_data.insert(this->memo_data.end(), mstr_key_bytes.begin(), mstr_key_bytes.end());	
		}
		else {
			this->memo_data.insert(this->memo_data.end(), pool_key_bytes.begin(), pool_key_bytes.end());
			this->memo_data.insert(this->memo_data.end(), farm_key_bytes.begin(), farm_key_bytes.end());
			this->memo_data.insert(this->memo_data.end(), mstr_key_bytes.begin(), mstr_key_bytes.end());	
		}
	}
	else {
		this->memo = Strip0x(this->memo);
		if (this->memo.size() % 2 != 0) {
			err.push_back("!! Invalid memo, should be only whole bytes (hex)");
			result = false;
		}
		this->memo_data = std::vector<uint8_t>(this->memo.size() / 2);
		HexToBytes(memo, this->memo_data.data());
	}
	this->memo = hexStr(this->memo_data);
	err.push_back("memo      = " + this->memo);

	if (this->plot_name.empty()) {
		time_t t = std::time(nullptr);
		std::tm tm = *std::localtime(&t);
		std::wostringstream oss;
		oss << std::put_time(&tm, L"%Y-%m-%d-%H-%M");
		std::wstring timestr = oss.str();

		this->plot_name = L"plot-k"+std::to_wstring((int)this->ksize)+L"-"+timestr+L"-"+s2ws(this->id)+L".plot";
	
	}
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

	if (this->ksize < 18) {
		this->ksize = 18;
	}
	if (this->ksize > 50) {
		this->ksize = 50;
	}

	if (this->buffer < 16) {
		this->buffer = 16;
	}

	if (this->buckets < 16) {
		this->buckets = 16;
	}
	if (this->buckets > 128) {
		this->buckets = 128;
	}

	if (this->stripes < 256) {
		this->stripes = 256;
	}

	if (this->threads < 1) {
		this->threads = 1;
	}

	return result;
}

JobCreatePlotRef::JobCreatePlotRef(std::string title, 
	std::string originalTitle,
	JobCreatePlotRefParam& param, 
	JobStartRuleParam& startRuleParam, 
	JobFinishRuleParam& finishRuleParam) : 
	JobCreatePlot(title, originalTitle, startRuleParam, finishRuleParam),
	param(param)
{
}

JobCreatePlotRef::JobCreatePlotRef(std::string title, std::string originalTitle) 
	: JobCreatePlot(title, originalTitle)
{

}

JobCreatePlotRef::JobCreatePlotRef(std::string title, std::string originalTitle, JobCreatePlotRefParam& param)
	:JobCreatePlot(title, originalTitle, JobStartRuleParam(), JobFinishRuleParam()),
	 param(param)
{

}

bool JobCreatePlotRef::drawEditor()
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

std::shared_ptr<Job> JobCreatePlotRef::relaunch()
{
	JobStartRule* startRule = dynamic_cast<JobStartRule*>(this->getStartRule());
	JobFinishRule* finishRule = dynamic_cast<JobFinishRule*>(this->getFinishRule());
	JobStartRuleParam& startParam = startRule->getRelaunchParam();
	JobFinishRuleParam& finishParam = finishRule->getRelaunchParam();
	if (!finishParam.repeatIndefinite && finishParam.repeatCount <= 0) {
		startParam.startPaused = true;
	}
	std::string oldTitle = this->getTitle();
	std::string newTitle = oldTitle.substr(0,oldTitle.find_first_of('#'));
	auto newJob = std::make_shared<JobCreatePlotRef>(
		this->getOriginalTitle()+"#"+systemClockToStr(std::chrono::system_clock::now()),
		this->getOriginalTitle(),
		this->param, 
		startParam,
		finishParam
	);
	return newJob;
}

void JobCreatePlotRef::initActivity()
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
				DiskPlotter plotter = DiskPlotter();
				plotter.context.job = this->shared_from_this();

				uint8_t phases_flags = 0;
				if (this->param.bitfield) {
					phases_flags = ENABLE_BITFIELD;
				}
				try {
					plotter.CreatePlotDisk(
							param.tempPathStr,
							param.temp2PathStr,
							param.destPathStr,
							param.filename,
							param.ksize,
							param.memo_data.data(),
							param.memo_data.size(),
							param.plot_id.data(),
							param.plot_id.size(),
							param.buffer,
							param.buckets,
							param.stripes,
							param.threads,
							!param.bitfield,
							false);
				}
				catch (...) {

				}
			}
		};
	}
}

bool JobCreatePlotRefParam::isValid(std::vector<std::string>& errs) const
{
	bool result = true;
	if (this->ksize < kMinPlotSize || this->ksize > kMaxPlotSize) {
		errs.push_back("Plot size k= " + std::to_string(this->ksize) + " is invalid");
		result = false;
	}

	uint32_t stripe_size, buf_megabytes, num_buckets;
	uint8_t num_threads;
	if (this->stripes != 0) {
		stripe_size = this->stripes;
	} else {
		stripe_size = 65536;
	}
	if (this->threads != 0) {
		num_threads = this->threads;
	} else {
		num_threads = 2;
	}
	if (this->buffer != 0) {
		buf_megabytes = this->buffer;
	} else {
		buf_megabytes = 4608;
	}

	if (buf_megabytes < 10) {
		errs.push_back("Please provide at least 10MiB of ram");
		result = false;
	}

	// Subtract some ram to account for dynamic allocation through the code
	uint64_t thread_memory = num_threads * (2 * (stripe_size + 5000)) *
								EntrySizes::GetMaxEntrySize(this->ksize, 4, true) / (1024 * 1024);
	uint64_t sub_mbytes = (5 + (int)std::min(buf_megabytes * 0.05, (double)50) + thread_memory);
	if (sub_mbytes > buf_megabytes) {
		errs.push_back(
			"Please provide more memory. At least " + std::to_string(sub_mbytes));
		result = false;
	}
	uint64_t memory_size = ((uint64_t)(buf_megabytes - sub_mbytes)) * 1024 * 1024;
	double max_table_size = 0;
	for (size_t i = 1; i <= 7; i++) {
		double memory_i = 1.3 * ((uint64_t)1 << this->ksize) * EntrySizes::GetMaxEntrySize(this->ksize, i, true);
		if (memory_i > max_table_size)
			max_table_size = memory_i;
	}
	if (this->buckets != 0) {
		num_buckets = RoundPow2(this->buckets);
	} else {
		num_buckets = 2 * RoundPow2(ceil(
								((double)max_table_size) / (memory_size * kMemSortProportion)));
	}

	if (num_buckets < kMinBuckets) {
		if (this->buckets != 0) {
			errs.push_back("Minimum buckets is " + std::to_string(kMinBuckets));
			result = false;
		}
		num_buckets = kMinBuckets;
	} else if (num_buckets > kMaxBuckets) {
		if (this->buckets != 0) {
			errs.push_back("Maximum buckets is " + std::to_string(kMaxBuckets));
			result = false;
		}
		double required_mem =
			(max_table_size / kMaxBuckets) / kMemSortProportion / (1024 * 1024) + sub_mbytes;
		errs.push_back(
			"Do not have enough memory. Need " + std::to_string(required_mem) + " MiB");
		result = false;
	}
	uint32_t log_num_buckets = log2(num_buckets);
	assert(log2(num_buckets) == ceil(log2(num_buckets)));

	if (max_table_size / num_buckets < stripe_size * 30) {
		errs.push_back("Stripe size too large");
	}

#if defined(_WIN32) || defined(__x86_64__)
	if (this->bitfield && !HavePopcnt()) {
		errs.push_back("Bitfield plotting not supported by CPU");
		result = false;
	}
#endif /* defined(_WIN32) || defined(__x86_64__) */			


	if (this->poolKey.empty() && this->puzzleHash.empty()) {
		errs.push_back("pool public key or puzzle hash must be specified");
		result = false;
	}
	else {
		if(!this->puzzleHash.empty()){
			if (this->poolKey.length() < 2*32) {
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

std::string JobCreatePlotRefFactory::getName()
{
	return "CreatePlot (reference plotter)";
}

std::shared_ptr<Job> JobCreatePlotRefFactory::create(std::string jobName)
{
	return std::make_shared<JobCreatePlotRef>(
		jobName, jobName, this->param, this->startRuleParam, this->finishRuleParam
	);
}

bool JobCreatePlotRefFactory::drawEditor()
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
