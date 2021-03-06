/*
 * phase2.hpp
 *
 *  Created on: May 29, 2021
 *      Author: mad
 */

#ifndef INCLUDE_CHIA_PHASE2_HPP_
#define INCLUDE_CHIA_PHASE2_HPP_

#include "phase2.h"
#include "DiskTable.h"
#include "ThreadPool.h"
#include "bitfield_index.hpp"

namespace mad::phase2 {

template<typename T, typename S, typename DS>
void compute_table(	int R_index, int num_threads,
					DS* R_sort, DiskTable<S>* R_file,
					const table_t& R_table,
					bitfield* L_used,
					const bitfield* R_used,
					DiskPlotterContext& context)
{
	const int num_threads_read = std::max(num_threads / 4, 2);
	
	DiskTable<T> R_input(R_table);
	{
		const auto begin = get_wall_time_micros();
		
		ThreadPool<std::pair<std::vector<T>, size_t>, size_t> pool(
			[L_used, R_used, &context](std::pair<std::vector<T>, size_t>& input, size_t&, size_t&) {
				uint64_t offset = 0;
				context.getCurrentTask()->totalWorkItem += input.first.size();
				for(const auto& entry : input.first) {
					if(R_used && !R_used->get(input.second + (offset++))) {
						context.getCurrentTask()->completedWorkItem++;
						continue;	// drop it
					}
					L_used->set(entry.pos);
					L_used->set(uint64_t(entry.pos) + entry.off);
					context.getCurrentTask()->completedWorkItem++;
				}
			}, nullptr, num_threads, "phase2/mark");
		
		L_used->clear();
		R_input.read(&pool, num_threads_read);
		pool.close();
		
		context.log("[P2] Table " + std::to_string(R_index) + " scan took "
				+ std::to_string((get_wall_time_micros() - begin) / 1e6) + " sec");
	}
	const auto begin = get_wall_time_micros();
	
	uint64_t num_written = 0;
	const bitfield_index index(*L_used);
	
	typedef typename DS::WriteCache WriteCache;
	
	Thread<std::vector<S>> R_write(
		[R_file, &context](std::vector<S>& input) {
			context.getCurrentTask()->totalWorkItem += input.size();
			for(auto& entry : input) {
				R_file->write(entry);
				context.getCurrentTask()->completedWorkItem++;
			}
		}, "phase2/write");
	
	ThreadPool<std::vector<S>, size_t, std::shared_ptr<WriteCache>> R_add(
		[R_sort,&context](std::vector<S>& input, size_t&, std::shared_ptr<WriteCache>& cache) {
			if(!cache) {
				cache = R_sort->add_cache();
			}
			context.getCurrentTask()->totalWorkItem += input.size();
			for(auto& entry : input) {
				cache->add(entry);
				context.getCurrentTask()->completedWorkItem++;
			}
		}, nullptr, std::max(num_threads / 2, 1), "phase2/add");
	
	Processor<std::vector<S>>* R_out = &R_add;
	if(R_file) {
		R_out = &R_write;
	}
	
	Thread<std::vector<S>> R_count(
		[R_out, &num_written, &context](std::vector<S>& input) {
			context.getCurrentTask()->totalWorkItem += input.size();
			for(auto& entry : input) {
				set_sort_key<S>{}(entry, num_written++);
				context.getCurrentTask()->completedWorkItem++;
			}
			R_out->take(input);
		}, "phase2/count");
	
	ThreadPool<std::pair<std::vector<T>, size_t>, std::vector<S>> map_pool(
		[&index, R_used, &context](std::pair<std::vector<T>, size_t>& input, std::vector<S>& out, size_t&) {
			out.reserve(input.first.size());
			uint64_t offset = 0;
			context.getCurrentTask()->totalWorkItem += input.first.size();
			for(const auto& entry : input.first) {
				if(R_used && !R_used->get(input.second + (offset++))) {
					context.getCurrentTask()->completedWorkItem++;
					continue;	// drop it
				}
				S tmp;
				tmp.assign(entry);
				const auto pos_off = index.lookup(entry.pos, entry.off);
				tmp.pos = pos_off.first;
				tmp.off = pos_off.second;
				out.push_back(tmp);
				context.getCurrentTask()->completedWorkItem++;
			}
		}, &R_count, num_threads*2, "phase2/remap");
	
	R_input.read(&map_pool, num_threads_read);
	
	map_pool.close();
	R_count.close();
	R_write.close();
	R_add.close();
	
	if(R_sort) {
		R_sort->finish();
	}
	if(R_file) {
		R_file->flush();
	}
	context.log("[P2] Table " + std::to_string(R_index) + " rewrite took "
				+ std::to_string((get_wall_time_micros() - begin) / 1e6) + " sec"
				+ ", dropped " +std::to_string( R_table.num_entries - num_written) + " entries"
				+ " (" + std::to_string(100 * (1 - double(num_written) / R_table.num_entries)) + " %)");
}

inline
void compute(	DiskPlotterContext& context,
				const phase1::output_t& input, output_t& out)
{
	const auto total_begin = get_wall_time_micros();
	
	const std::wstring prefix = input.tempDir + input.plot_name + L".p2.";
	const std::wstring prefix_2 = input.tempDir2 + input.plot_name + L".p2.";

	std::shared_ptr<JobTaskItem> currentTask = context.getCurrentTask();
	context.pushTask("Phase2.Table2", currentTask);
	context.pushTask("Phase2.Table3", currentTask);
	context.pushTask("Phase2.Table4", currentTask);
	context.pushTask("Phase2.Table5", currentTask);
	context.pushTask("Phase2.Table6", currentTask);
	context.pushTask("Phase2.Table7", currentTask);
	
	context.getCurrentTask()->start();
	size_t max_table_size = 0;
	for(const auto& table : input.table) {
		max_table_size = std::max(max_table_size, table.num_entries);
	}
	context.log("[P2] max_table_size = " + std::to_string(max_table_size));
	
	auto curr_bitfield = std::make_shared<bitfield>(max_table_size);
	auto next_bitfield = std::make_shared<bitfield>(max_table_size);
	
	DiskTable<entry_7> table_7(prefix_2 + L"table7.tmp");
	
	compute_table<entry_7, entry_7, DiskSort7>(
			7, input.num_threads, nullptr, &table_7, input.table[6], next_bitfield.get(), nullptr, context);
	
	table_7.close();
	_wremove(input.table[6].file_name.c_str());

	context.popTask();
	
	for(int i = 5; i >= 1; --i)
	{
		context.getCurrentTask()->start();
		std::swap(curr_bitfield, next_bitfield);
		out.sort[i] = std::make_shared<DiskSortT>(32, input.log_num_buckets, prefix + L"t" + std::to_wstring(i + 1));
		
		compute_table<phase1::tmp_entry_x, entry_x, DiskSortT>(
			i + 1, input.num_threads, out.sort[i].get(), nullptr, input.table[i], next_bitfield.get(), curr_bitfield.get(), context);
		
		_wremove(input.table[i].file_name.c_str());
		context.popTask();
	}
	
	out.params = input.params;
	out.table_1 = input.table[0];
	out.table_7 = table_7.get_info();
	out.bitfield_1 = next_bitfield;
	out.num_threads = input.num_threads;
	out.plot_name = input.plot_name;
	out.log_num_buckets = input.log_num_buckets;
	out.tempDir = input.tempDir;
	out.tempDir2 = input.tempDir2;
	
	context.log("Phase 2 took " + std::to_string((get_wall_time_micros() - total_begin) / 1e6) + " sec");
}


} // phase2

#endif /* INCLUDE_CHIA_PHASE2_HPP_ */
