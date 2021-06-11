#include "JobCreatePlot.h"
#include "ImFrame.h"
#include "imgui.h"
#include "gui.hpp"
#include "Imgui/misc/cpp/imgui_stdlib.h"
#include "chiapos/pos_constants.hpp"
#include "chiapos/entry_sizes.hpp"

JobCreatePlot::JobCreatePlot(std::string title, const JobCreatePlotParam& param)
	: Job(title), param(param)
{	
	this->init();
	this->startRule.param = param.startRuleParam;
	this->finishRule.param = param.finishRuleParam;
	this->jobEditor.setData(&this->param);
}

JobCreatePlot::JobCreatePlot(std::string title) : Job(title)
{
	this->init();
}

void JobCreatePlot::init()
{
	this->jobProgress = std::make_shared<JobProgress>(this->getTitle());
	this->startEvent = std::make_shared<JobEvent>("job-start");
	this->finishEvent = std::make_shared<JobEvent>("job-finish");
	this->phase1FinishEvent = std::make_shared<JobEvent>("phase1-finish");
	this->phase2FinishEvent = std::make_shared<JobEvent>("phase2-finish");
	this->phase3FinishEvent = std::make_shared<JobEvent>("phase3-finish");
	this->phase4FinishEvent = std::make_shared<JobEvent>("phase4-finish");
	this->events.push_back(this->startEvent);
	this->events.push_back(this->finishEvent);
	this->events.push_back(this->phase1FinishEvent);
	this->events.push_back(this->phase2FinishEvent);
	this->events.push_back(this->phase3FinishEvent);
	this->events.push_back(this->phase4FinishEvent);
}

JobCreatePlotParam* JobCreatePlot::drawUI()
{
	static WidgetCreatePlot createPlotWidget;
	static JobCreatePlotParam createPlotParam;
	createPlotWidget.setData(&createPlotParam);
	if (createPlotWidget.draw()) {
		return createPlotWidget.getData();
	}
	else {
		return nullptr;
	}
}

int JobCreatePlot::jobIdCounter = 1;

bool JobCreatePlot::isRunning() const {
	return this->running;
}

bool JobCreatePlot::isFinished() const {
	return this->finished;
}

bool JobCreatePlot::isPaused() const {
	return this->paused;
}

bool JobCreatePlot::start() {
	this->running = true;
	this->paused = false;
	return true;
}

bool JobCreatePlot::pause() {
	this->paused = true;
	return true;
}

bool JobCreatePlot::cancel() {
	this->running = false;
	this->paused = false;
	return false;
}

float JobCreatePlot::getProgress() {
	this->progress += 0.01f;
	if (this->progress > 1.0f) {
		this->progress = 0.0f;
	}
	return this->progress;
}

JobRule& JobCreatePlot::getStartRule() {
	return this->startRule;
}

JobRule& JobCreatePlot::getFinishRule() {
	return this->finishRule;
}

void JobCreatePlot::drawItemWidget() {
	Job::drawItemWidget();		

	if (!this->isRunning()) {			
		if (this->startRule.drawItemWidget()) {
			ImGui::ScopedSeparator();
		}
	}
	this->finishRule.drawItemWidget();
}

void JobCreatePlot::drawStatusWidget() {
	Job::drawStatusWidget();
	ImGui::ScopedSeparator();
	if (!this->isRunning()) {
		this->jobEditor.draw();
	}		
}

bool JobCreatePlot::preStartCheck(std::vector<std::string>& errs) {
	bool result = true;
	if (this->param.ksize < kMinPlotSize || this->param.ksize > kMaxPlotSize) {
		errs.push_back("Plot size k= " + std::to_string(this->param.ksize) + " is invalid");
		result = false;
	}

	uint32_t stripe_size, buf_megabytes, num_buckets;
	uint8_t num_threads;
	if (this->param.stripes != 0) {
		stripe_size = this->param.stripes;
	} else {
		stripe_size = 65536;
	}
	if (this->param.threads != 0) {
		num_threads = this->param.threads;
	} else {
		num_threads = 2;
	}
	if (this->param.buffer != 0) {
		buf_megabytes = this->param.buffer;
	} else {
		buf_megabytes = 4608;
	}

	if (buf_megabytes < 10) {
		errs.push_back("Please provide at least 10MiB of ram");
		result = false;
	}

	// Subtract some ram to account for dynamic allocation through the code
	uint64_t thread_memory = num_threads * (2 * (stripe_size + 5000)) *
								EntrySizes::GetMaxEntrySize(this->param.ksize, 4, true) / (1024 * 1024);
	uint64_t sub_mbytes = (5 + (int)std::min(buf_megabytes * 0.05, (double)50) + thread_memory);
	if (sub_mbytes > buf_megabytes) {
		errs.push_back(
			"Please provide more memory. At least " + std::to_string(sub_mbytes));
		result = false;
	}
	uint64_t memory_size = ((uint64_t)(buf_megabytes - sub_mbytes)) * 1024 * 1024;
	double max_table_size = 0;
	for (size_t i = 1; i <= 7; i++) {
		double memory_i = 1.3 * ((uint64_t)1 << this->param.ksize) * EntrySizes::GetMaxEntrySize(this->param.ksize, i, true);
		if (memory_i > max_table_size)
			max_table_size = memory_i;
	}
	if (this->param.buckets != 0) {
		num_buckets = RoundPow2(this->param.buckets);
	} else {
		num_buckets = 2 * RoundPow2(ceil(
								((double)max_table_size) / (memory_size * kMemSortProportion)));
	}

	if (num_buckets < kMinBuckets) {
		if (this->param.buckets != 0) {
			errs.push_back("Minimum buckets is " + std::to_string(kMinBuckets));
			result = false;
		}
		num_buckets = kMinBuckets;
	} else if (num_buckets > kMaxBuckets) {
		if (this->param.buckets != 0) {
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
	if (this->param.bitfield && !HavePopcnt()) {
		errs.push_back("Bitfield plotting not supported by CPU");
		result = false;
	}
#endif /* defined(_WIN32) || defined(__x86_64__) */			
	return result;
}

WidgetCreatePlot::WidgetCreatePlot() {
}

bool WidgetCreatePlot::draw()
{
	bool result = false;
	ImGui::PushID((const void*)this);
	float fieldWidth = ImGui::GetWindowContentRegionWidth();

	ImGui::PushItemWidth(80.0f);
	ImGui::Text("Pool Key");
	ImGui::PopItemWidth();
	ImGui::SameLine(80.0f);
	ImGui::PushItemWidth(fieldWidth-(80.0f + 55.0f));
	result |= ImGui::InputText("##poolkey", &this->param->poolKey,ImGuiInputTextFlags_CharsHexadecimal);
	if (ImGui::IsItemHovered() && !this->param->poolKey.empty()) {
		ImGui::BeginTooltip();
		tooltiipText(this->param->poolKey.c_str());
		ImGui::EndTooltip();
	}
	ImGui::PopItemWidth();
	ImGui::SameLine();
	ImGui::PushItemWidth(55.0f);
	if (ImGui::Button("Paste##pool")) {
		this->param->poolKey = ImGui::GetClipboardText();
		result |= true;
	}
	ImGui::PopItemWidth();

	ImGui::PushItemWidth(80.0f);
	ImGui::Text("Farm Key");
	ImGui::PopItemWidth();
	ImGui::SameLine(80.0f);
	ImGui::PushItemWidth(fieldWidth-(80.0f + 55.0f));
	ImGui::InputText("##farmkey", &this->param->farmKey,ImGuiInputTextFlags_CharsHexadecimal);
	if (ImGui::IsItemHovered() && !this->param->farmKey.empty()) {
		ImGui::BeginTooltip();
		tooltiipText(this->param->farmKey.c_str());
		ImGui::EndTooltip();
	}
	ImGui::PopItemWidth();
	ImGui::SameLine();
	ImGui::PushItemWidth(55.0f);
	if (ImGui::Button("Paste##farm")) {
		this->param->farmKey = ImGui::GetClipboardText();
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
		this->param->tempPath = tempDir;
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
		this->param->tempPath = tempDir;
		result |= true;
	}
	ImGui::PopItemWidth();
	ImGui::PushItemWidth(55.0f);
	ImGui::SameLine();
	if (ImGui::Button("Select##tempDir")) {
		std::optional<std::filesystem::path> dirPath = ImFrame::PickFolderDialog();
		if (dirPath) {
			tempDir = dirPath->string();
			this->param->tempPath = tempDir;
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
		this->param->destPath = destDir;
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
		this->param->destPath = destDir;
		result |= true;
	}
	ImGui::PopItemWidth();
	ImGui::PushItemWidth(55.0f);
	ImGui::SameLine();
	if (ImGui::Button("Select##destDir")) {
		std::optional<std::filesystem::path> dirPath = ImFrame::PickFolderDialog();
		if (dirPath) {
			destDir = dirPath.value().string();
			this->param->destPath = dirPath.value();
		}
		result |= true;
	}
	ImGui::PopItemWidth();

	if (ImGui::CollapsingHeader("Advanced")) {
		ImGui::Indent(20.0f);
		fieldWidth = ImGui::GetWindowContentRegionWidth();

		ImGui::Text("Plot Size (k)");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-130.0f);
		static int kinput = (int)this->param->ksize;
		if (ImGui::InputInt("##k", &kinput, 1, 10)) {
			this->param->ksize = (uint8_t)kinput;
			if (this->param->ksize < 18) {
				this->param->ksize = 18;
			}
			if (this->param->ksize > 50) {
				this->param->ksize = 50;
			}				
			result |= true;
		}
		ImGui::PopItemWidth();
			
		ImGui::Text("Temp Dir2");
		ImGui::SameLine(120.0f);
		static std::string tempDir2;
		ImGui::PushItemWidth(fieldWidth-225.0f);
		if (ImGui::InputText("##tempDir2", &tempDir2)) {
			this->param->temp2Path = tempDir2;
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
			this->param->temp2Path = tempDir2;
			result |= true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Select##tempDir2")) {
			std::optional<std::filesystem::path> dirPath = ImFrame::PickFolderDialog();
			if (dirPath) {
				tempDir2 = dirPath.value().string();
				this->param->temp2Path = dirPath.value();
			}
			result |= true;
		}
		ImGui::PopItemWidth();

		ImGui::Text("Buckets");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-130.0f);
		static int bucketInput = (int)this->param->buckets;
		if (ImGui::InputInt("##buckets", &bucketInput, 1, 8)) {
			this->param->buckets = (uint32_t)bucketInput;
			if (this->param->buckets < 16) {
				this->param->buckets = 16;
			}
			if (this->param->buckets > 128) {
				this->param->buckets = 128;
			}
			bucketInput= this->param->buckets;
			result |= true;
		}

		ImGui::Text("Stripes");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-130.0f);
		static int stripesInput = (int)this->param->stripes;
		if (ImGui::InputInt("##stripes", &stripesInput, 1024, 4096)) {
			this->param->stripes = stripesInput;
			if (this->param->stripes < 1) {
				this->param->stripes = 1;
			}
			result |= true;
		}
		ImGui::PopItemWidth();

		ImGui::Text("Threads");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-130.0f);
		static int threadsInput = (int)this->param->threads;
		if (ImGui::InputInt("##threads", &threadsInput, 1, 2)) {
			this->param->threads = (uint8_t)threadsInput;
			if (this->param->threads < 1) {
				this->param->threads = 1;
			}
			result |= true;
		}
		ImGui::PopItemWidth();

		ImGui::Text("Buffer (MB)");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-130.0f);
		static int bufferInput = (int)this->param->buffer;
		if (ImGui::InputInt("##buffer", &bufferInput, 1024, 2048)) {
			this->param->buffer = bufferInput;
			if (this->param->buffer < 16) {
				this->param->buffer = 16;
			}
			result |= true;
		}
		ImGui::PopItemWidth();

		ImGui::Text("Bitfield");
		ImGui::SameLine(120.0f);
		ImGui::PushItemWidth(fieldWidth-130.0f);
		ImGui::Checkbox("##bitfeld", &this->param->bitfield);
		ImGui::PopItemWidth();

		if (ImGui::Button("Load Default")) {
			this->param->loadDefault();
			result |= true;
		}

		ImGui::Unindent(20.0f);
	}

	if (ImGui::CollapsingHeader("Start Rule")) {
		ImGui::Indent(20.0f);
		result |= this->startRuleWidget.draw();
		ImGui::Unindent(20.0f);
	}

	if (ImGui::CollapsingHeader("Finish Rule")) {
		ImGui::Indent(20.0f);
		result |= this->finishRuleWidget.draw();
		ImGui::Unindent(20.0f);
	}

	std::string errMsg = "";
	result = this->param->isValid(&errMsg);

	if (!errMsg.empty()) {
		ImGui::Text(errMsg.c_str());
	}

	ImGui::PopID();
	return result;
}

void WidgetCreatePlot::setData(JobCreatePlotParam* param)
{
	Widget<JobCreatePlotParam*>::setData(param);
	this->startRuleWidget.setData(&param->startRuleParam);
	this->finishRuleWidget.setData(&param->finishRuleParam);
}

void JobCreatePlotParam::loadDefault()
{
	this->buckets = 128;
	this->stripes = 65536;
	this->threads = 2;
	this->buffer = 4608;
	this->ksize = 32;
	this->temp2Path = "";
	this->bitfield = true;
}

bool JobCreatePlotParam::isValid(std::string* errMsg) const {
	if (errMsg) {
		if (this->poolKey.empty()) {
			*errMsg = "pool public key must be specified";
		}
		else {
			if (this->poolKey.length() < 2*48) {
				*errMsg = "pool public key must be 48 bytes (96 hex characters)";
			}
		}
		if (this->farmKey.empty()) {
			*errMsg = "farm public key must be specified";
		}
		else {
		if (this->farmKey.length() < 2*48) {
				*errMsg = "farm public key must be 48 bytes (96 hex characters)";
			}
		}
	}
	return !this->poolKey.empty() && !this->farmKey.empty();
}

bool WidgetCreatePlotStartRule::draw()
{
	ImGui::PushID((const void*)this);
	bool result = false;
	if (ImGui::Checkbox("Paused", &this->param->startPaused)) {
		this->param->startDelayed = false;
		this->param->startConditional = false;
		this->param->startImmediate = false;
		result |= true;
	}
	if (ImGui::Checkbox("Immediately", &this->param->startImmediate)) {
		this->param->startDelayed = false;
		this->param->startConditional = false;
		this->param->startPaused = false;
		result |= true;
	}
	if (ImGui::Checkbox("Delayed",&this->param->startDelayed)) {
		this->param->startImmediate = false;
		this->param->startConditional = false;
		this->param->startPaused = false;
		result |= true;
	}
	if (this->param->startDelayed) {
		ImGui::Indent(20.0f);
		float fieldWidth = ImGui::GetWindowContentRegionWidth();
		ImGui::BeginGroupPanel(ImVec2(-1.0f,0.0f));
		ImGui::Text("Delay (min)");
		ImGui::SameLine(80.0f);
		ImGui::PushItemWidth(fieldWidth-160.0f);
		if (ImGui::InputInt("##delay", &this->param->startDelayedMinute, 1, 5)) {
			if (this->param->startDelayedMinute < 1) {
				this->param->startDelayedMinute = 1;
			}
			result |= true;
		}
		ImGui::PopItemWidth();
		ImGui::EndGroupPanel();
		ImGui::Unindent(20.0f);
	}
	if (ImGui::Checkbox("Conditional",&this->param->startConditional)) {
		this->param->startImmediate = false;
		this->param->startDelayed = false;
		this->param->startPaused = false;
		result |= true;
	}
	if (this->param->startConditional) {
		ImGui::Indent(20.0f);
		ImGui::BeginGroupPanel(ImVec2(-1.0f,0.0f));
			
		result |= ImGui::Checkbox("if Active Jobs",&this->param->startCondActiveJob);
		if (this->param->startCondActiveJob) {
			float fieldWidth = ImGui::GetWindowContentRegionWidth();
			ImGui::Text("Less Than");
			ImGui::SameLine(80.0f);
			ImGui::PushItemWidth(fieldWidth-150.0f);
			if (ImGui::InputInt("##lessthan", &this->param->startCondActiveJobCount, 1, 5)) {
				if (this->param->startCondActiveJobCount < 1) {
					this->param->startCondActiveJobCount = 1;
				}
				result |= true;
			}
			ImGui::PopItemWidth();
		}
		result |= ImGui::Checkbox("If Time Within",&this->param->startCondTime);
		if (this->param->startCondTime) {
			float fieldWidth = ImGui::GetWindowContentRegionWidth();
			ImGui::Text("Start (Hr)");
			ImGui::SameLine(80.0f);
			ImGui::PushItemWidth(fieldWidth-150.0f);
			if (ImGui::InputInt("##start", &this->param->startCondTimeStart, 1, 5)) {
				if (this->param->startCondTimeStart < 0) {
					this->param->startCondTimeStart = 24;
				}
				if (this->param->startCondTimeStart > 24) {
					this->param->startCondTimeStart = 0;
				}
				result |= true;
			}
			ImGui::PopItemWidth();
			ImGui::Text("End (Hr)");
			ImGui::SameLine(80.0f);
			ImGui::PushItemWidth(fieldWidth-150.0f);
			if (ImGui::InputInt("##end", &this->param->startCondTimeEnd, 1, 5)) {
				if (this->param->startCondTimeEnd < 0) {
					this->param->startCondTimeEnd = 24;
				}
				if (this->param->startCondTimeEnd > 24) {
					this->param->startCondTimeEnd = 0;
				}
				result |= true;
			}
			ImGui::PopItemWidth();
		}
			
		ImGui::EndGroupPanel();
		ImGui::Unindent(20);
	}
	if (!this->param->startImmediate && !this->param->startDelayed && !this->param->startConditional && !this->param->startPaused) {
		this->param->startPaused = true;
	}
	ImGui::PopID();
	return result;
}

bool WidgetCreatePlotFinishRule::draw() {
	ImGui::PushID((const void*)this);
	bool result = false;
	result |= ImGui::Checkbox("Relaunch",&this->param->repeatJob);
	if (this->param->repeatJob) {
		ImGui::Indent(20.0f);
		ImGui::BeginGroupPanel(ImVec2(-1.0f,0.0f));
		if (!this->param->repeatIndefinite) {
			float fieldWidth = ImGui::GetWindowContentRegionWidth();
			ImGui::Text("Count");
			ImGui::SameLine(80.0f);
			ImGui::PushItemWidth(fieldWidth-150.0f);
			if (ImGui::InputInt("##repeatCount", &this->param->repeatCount, 1, 5)) {
				if (this->param->repeatCount < 1) {
					this->param->repeatCount = 1;
				}
				result |= true;
			}
			ImGui::PopItemWidth();
		}
		result |= ImGui::Checkbox("Indefinite",&this->param->repeatIndefinite);
		ImGui::EndGroupPanel();
		ImGui::Unindent(20.0f);
	}
	result |= ImGui::Checkbox("Launch Program",&this->param->execProg);
	if (this->param->execProg) {
		ImGui::Indent(20.0f);
		ImGui::BeginGroupPanel(ImVec2(-1.0f,0.0f));
			float fieldWidth = ImGui::GetWindowContentRegionWidth();
			ImGui::Text("Path");
			ImGui::SameLine(80.0f);
			static std::string execPath;
			ImGui::PushItemWidth(fieldWidth-240.0f);
			if (ImGui::InputText("##execPath", &execPath)) {
				this->param->progToExec = execPath;
				result |= true;
			}
			if (ImGui::IsItemHovered() && !execPath.empty()) {
				ImGui::BeginTooltip();
				tooltiipText(execPath.c_str());
				ImGui::EndTooltip();
			}
			ImGui::PopItemWidth();
			ImGui::SameLine();
			ImGui::PushItemWidth(44.0f);
			if (ImGui::Button("Paste##execPath")) {
				execPath = ImGui::GetClipboardText();
				this->param->progToExec = execPath;
				result |= true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Select##execPath")) {
				std::vector<ImFrame::Filter> filters;
				filters.push_back(ImFrame::Filter());
				filters.push_back(ImFrame::Filter());
				filters[0].name = "exe";
				filters[0].spec = "*.exe";
				filters[1].name = "batch script";
				filters[1].spec = "*.bat";
				std::optional<std::filesystem::path> dirPath = ImFrame::OpenFileDialog(filters);
				if (dirPath) {
					execPath = dirPath->string();
					this->param->progToExec = dirPath.value();
				}
				result |= true;
			}
			ImGui::PopItemWidth();
		ImGui::EndGroupPanel();
		ImGui::Unindent(20.0f);
	}
	ImGui::PopID();
	return result;
}

JobCratePlotStartRule::JobCratePlotStartRule()
{
	this->creationTime = std::chrono::system_clock::now();
}

bool JobCratePlotStartRule::drawItemWidget()
{
	if (this->param.startDelayed) {
		std::chrono::minutes delayDuration(this->param.startDelayedMinute);
		std::chrono::time_point<std::chrono::system_clock> startTime = this->creationTime + delayDuration;
		std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();
		std::chrono::duration<float> delta = startTime - currentTime;
		ImGui::Text("will start in %.1f hour ", delta.count()/3600.0f);
		return true;
	}
	else if (this->param.startPaused) {
		ImGui::Text("job paused");
		return true;
	}
	else if (this->param.startConditional) {
		bool result = false;
		if (this->param.startCondActiveJob) {
			ImGui::Text("start if active job < %d", this->param.startCondActiveJobCount);
			result |= true;
		}
		if (this->param.startCondTime) {
			ImGui::Text("start within %02d:00 - %2d:00", this->param.startCondTimeStart, this->param.startCondTimeEnd);
			result |= true;
		}
		return result;
	}
	return false;
}

bool JobCratePlotStartRule::evaluate() {
	if (this->param.startDelayed) {
		std::chrono::minutes delayDuration(this->param.startDelayedMinute);
		std::chrono::time_point<std::chrono::system_clock> startTime = this->creationTime + delayDuration;
		std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();
		return currentTime > startTime;
	}
	else if (this->param.startConditional) {
		bool result = true;
		if (this->param.startCondTime) {
			std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();
			std::time_t ct = std::chrono::system_clock::to_time_t(currentTime);
			std::tm* t = std::localtime(&ct);
			if ((this->param.startCondTimeStart > t->tm_hour) && 
				(t->tm_hour < this->param.startCondTimeEnd)) {
				result &= true;
			}
			else {
				result &= false;
			}
		}

		if (this->param.startCondActiveJob) {
			if (JobManager::getInstance().countRunningJob() < this->param.startCondActiveJobCount) {
				result &= true;
			}
			else {
				result &= false;
			}
		}
		return result;
	}
	return true;
}

JobCratePlotStartRuleParam::JobCratePlotStartRuleParam() {
		
}

bool JobCreatePlotFinishRule::drawItemWidget() {
	if (this->param.repeatJob) {
		if (this->param.repeatIndefinite) {
			ImGui::Text("Job will repeat indefinitely");
			return true;
		}
		else {
			ImGui::Text("Job will repeat %d times", this->param.repeatCount);
			return true;
		}
	}
	return false;
}

std::shared_ptr<JobTaskItem> CreatePlotContext::getCurrentTask()
{
	if (this->tasks.empty()) {
		return this->job->jobProgress;
	}
	else {
		return this->tasks.top();
	}
}

std::shared_ptr<JobTaskItem> CreatePlotContext::pushTask(std::string name, uint32_t totalWorkItem)
{
	std::shared_ptr<JobTaskItem> newTask = std::make_shared<JobTaskItem>(name);
	newTask->totalWorkItem = totalWorkItem;
	this->getCurrentTask()->addChild(newTask);
	this->tasks.push(newTask);
	return newTask;
}

std::shared_ptr<JobTaskItem> CreatePlotContext::popTask()
{
	std::shared_ptr<JobTaskItem> result = this->tasks.top();
	this->tasks.pop();
	return result;
}
