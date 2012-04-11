#include <algorithm> // for greater, sort
#include <numeric>
#include <functional>
#include <limits.h>
#include <iostream>

#include "sharedres.h"
#include "res_io.h"

#include "time-types.h"
#include "math-helper.h"

#include "stl-helper.h"

#ifdef CONFIG_USE_0X
#include <unordered_map>
#define hashmap std::unordered_map
#else
#include <ext/hash_map>
#define hashmap __gnu_cxx::hash_map
#endif

static const unsigned int UNLIMITED = UINT_MAX;

std::ostream& operator<<(std::ostream &os, const TaskInfo  &ti)
{
	os << "TaskInfo[";
	if (ti.get_priority() != UINT_MAX)
		os << "priority="
		   << ti.get_priority() <<  ", ";
	os << "period="
	   << ti.get_period()   << ", response="
	   << ti.get_response() << ", cluster="
	   << ti.get_cluster()  << ", requests=<";

	foreach(ti.get_requests(), it)
	{
		if (it != ti.get_requests().begin())
			os << " ";
		os << (*it);
	}

	os << ">]";
	return os;
}

std::ostream& operator<<(std::ostream &os, const RequestBound &rb)
{
	os << "(res-id="
	   << rb.get_resource_id() << ", num="
	   << rb.get_num_requests() << ", len="
	   << rb.get_request_length() << ")";
	return os;
}

std::ostream& operator<<(std::ostream &os, const ResourceSharingInfo &rsi)
{
	foreach(rsi.get_tasks(), it)
	{
		const TaskInfo& tsk  = *it;
		os << "\t" << tsk << std::endl;
	}
	return os;
}

unsigned int RequestBound::get_max_num_requests(unsigned long interval) const
{
	unsigned long num_jobs;

	num_jobs = divide_with_ceil(interval + task->get_response(),
				    task->get_period());

	return (unsigned int) (num_jobs * num_requests);
}


// ****** non-exported helpers *******

typedef std::vector<const TaskInfo*> Cluster;
typedef std::vector<Cluster> Clusters;

static void split_by_cluster(const ResourceSharingInfo& info, Clusters& clusters)
{
	foreach(info.get_tasks(), it)
	{
		const TaskInfo& tsk  = *it;
		unsigned int cluster = tsk.get_cluster();

		while (cluster  >= clusters.size())
			clusters.push_back(Cluster());

		clusters[cluster].push_back(&tsk);
	}
}


bool has_higher_priority(const TaskInfo* a, const TaskInfo* b)
{
	return a->get_priority() < b->get_priority();
}

void sort_by_priority(Clusters& clusters)
{
	foreach(clusters, it)
	{
		Cluster& cluster = *it;
		std::sort(cluster.begin(), cluster.end(), has_higher_priority);
	}
}

typedef std::vector<const RequestBound*> ContentionSet;
typedef std::vector<ContentionSet> Resources;
typedef std::vector<Resources> ClusterResources;

typedef std::vector<ContentionSet> AllPerCluster;


struct LimitedRequestBound {
	LimitedRequestBound(const RequestBound *rqb, unsigned int l) :
		request_bound(rqb), limit(l) {};
	LimitedRequestBound() : request_bound(NULL), limit(0) {};

	const RequestBound  *request_bound;
	unsigned int        limit;
};

typedef std::vector<LimitedRequestBound> LimitedContentionSet;



static void split_by_resource(const ResourceSharingInfo& info, Resources& resources)
{
	foreach(info.get_tasks(), it)
	{
		const TaskInfo& tsk  = *it;

		foreach(tsk.get_requests(), jt)
		{
			const RequestBound& req = *jt;
			unsigned int res = req.get_resource_id();

			while (res  >= resources.size())
				resources.push_back(ContentionSet());

			resources[res].push_back(&req);
		}
	}
}

static void all_from_cluster(const Cluster& cluster, ContentionSet& cs)
{
	foreach(cluster, it)
	{
		const TaskInfo* tsk  = *it;

		foreach(tsk->get_requests(), jt)
		{
			const RequestBound& req = *jt;
			cs.push_back(&req);
		}
	}
}

static void all_per_cluster(const Clusters& clusters,
			    AllPerCluster& all)
{
	foreach(clusters, it)
	{
		all.push_back(ContentionSet());
		all_from_cluster(*it, all.back());
	}
}


static void split_by_resource(const Cluster& cluster, Resources& resources)
{

	foreach(cluster, it)
	{
		const TaskInfo* tsk  = *it;

		foreach(tsk->get_requests(), jt)
		{
			const RequestBound& req = *jt;
			unsigned int res = req.get_resource_id();

			while (res  >= resources.size())
				resources.push_back(ContentionSet());

			resources[res].push_back(&req);
		}
	}
}

static void split_by_resource(const Clusters& clusters,
			      ClusterResources& resources)
{
	foreach(clusters, it)
	{
		resources.push_back(Resources());
		split_by_resource(*it, resources.back());
	}
}

static void split_by_type(const ContentionSet& requests,
			  ContentionSet& reads,
			  ContentionSet& writes)
{
	foreach(requests, it)
	{
		const RequestBound *req = *it;

		if (req->get_request_type() == READ)
			reads.push_back(req);
		else
			writes.push_back(req);
	}
}

static void split_by_type(const Resources& resources,
			  Resources &reads,
			  Resources &writes)
{
	reads.reserve(resources.size());
	writes.reserve(resources.size());
	foreach(resources, it)
	{
		reads.push_back(ContentionSet());
		writes.push_back(ContentionSet());
		split_by_type(*it, reads.back(), writes.back());
	}
}

static void split_by_type(const ClusterResources& per_cluster,
			  ClusterResources &reads,
			  ClusterResources &writes)
{
	reads.reserve(per_cluster.size());
	writes.reserve(per_cluster.size());
	foreach(per_cluster, it)
	{
		reads.push_back(Resources());
		writes.push_back(Resources());
		split_by_type(*it, reads.back(), writes.back());
	}
}

static bool has_longer_request_length(const RequestBound* a,
				      const RequestBound* b)
{
	return a->get_request_length() > b->get_request_length();
}

static void sort_by_request_length(ContentionSet& cs)
{
	std::sort(cs.begin(), cs.end(), has_longer_request_length);
}

static bool has_longer_request_length_lcs(const LimitedRequestBound &a,
				          const LimitedRequestBound &b)
{
	return has_longer_request_length(a.request_bound, b.request_bound);
}

static void sort_by_request_length(LimitedContentionSet &lcs)
{
	std::sort(lcs.begin(), lcs.end(), has_longer_request_length_lcs);
}

static void sort_by_request_length(Resources& resources)
{
	apply_foreach(resources, sort_by_request_length);
}

static void sort_by_request_length(ClusterResources& resources)
{
	apply_foreach(resources, sort_by_request_length);
}

typedef std::vector<ContentionSet> TaskContention;
typedef std::vector<TaskContention> ClusterContention;

// have one contention set per task
static void derive_task_contention(const Cluster& cluster,
				   TaskContention& requests)
{
	requests.reserve(cluster.size());

	foreach(cluster, it)
	{
		const TaskInfo* tsk  = *it;

		requests.push_back(ContentionSet());

		foreach(tsk->get_requests(), jt)
		{
			const RequestBound& req = *jt;

			requests.back().push_back(&req);
		}
	}
}

static void derive_task_contention(const Clusters& clusters,
				   ClusterContention& contention)
{
	map_ref(clusters, contention, TaskContention, derive_task_contention);
}

static Interference bound_blocking(const ContentionSet& cont,
				   unsigned long interval,
				   unsigned int max_total_requests,
				   unsigned int max_requests_per_source,
				   const TaskInfo* exclude_tsk,
				   // Note: the following parameter excludes
				   // *high-priority* tasks. Used to exclude local higher-priority tasks.
				   // Default: all tasks can block (suitable for remote blocking).
				   unsigned int min_priority = 0)
{
	Interference inter;
	unsigned int remaining;

	remaining = max_total_requests;

	foreach(cont, it)
	{
		const RequestBound* req = *it;

		if (!remaining)
			break;

		// only use this source if it is not excluded
		if (req->get_task() != exclude_tsk &&
		    req->get_task()->get_priority() >= min_priority)
		{
			unsigned int num;
			// This makes the assumption that there is only one
			// request object per task. This makes sense if the
			// contention set has been split by resource. This may
			// be pessimistic for contention sets that contain
			// request objects for multiple resources. The
			// assumption also works out if max_total_requests ==
			// max_requests_per_source.
			num = std::min(req->get_max_num_requests(interval),
				       max_requests_per_source);
			num = std::min(num, remaining);

			inter.total_length += num * req->get_request_length();
			inter.count        += num;
			remaining -= num;
		}
	}

	return inter;
}

static void add_blocking(LimitedContentionSet &lcs,
			 const ContentionSet& cont,
			 unsigned long interval,
			 unsigned int max_total_requests,
			 unsigned int max_requests_per_source,
			 const TaskInfo* exclude_tsk,
			 unsigned int min_priority = 0)
{
	unsigned int remaining;

	remaining = max_total_requests;

	foreach(cont, it)
	{
		const RequestBound* req = *it;

		if (!remaining)
			break;

		// only use this source if it is not excluded
		if (req->get_task() != exclude_tsk &&
		    req->get_task()->get_priority() >= min_priority)
		{
			unsigned int num;
			// This makes the assumption that there is only one
			// request object per task. See bound_blocking() above.
			num = std::min(req->get_max_num_requests(interval),
				       max_requests_per_source);
			num = std::min(num, remaining);
			remaining -= num;
			lcs.push_back(LimitedRequestBound(req, num));
		}
	}
}

static Interference bound_blocking(const ContentionSet& cont,
				   unsigned long interval,
				   unsigned int max_total_requests,
				   unsigned int max_requests_per_source,
				   bool exclude_whole_cluster,
				   const TaskInfo* exclude_tsk)
{
	Interference inter;
	unsigned int remaining;

	remaining = max_total_requests;

	foreach(cont, it)
	{
		const RequestBound* req = *it;

		if (!remaining)
			break;

		// only use this source if it is not excluded
		if (req->get_task() != exclude_tsk &&
		    (!exclude_whole_cluster ||
		     req->get_task()->get_cluster() != exclude_tsk->get_cluster()))
		{
			unsigned int num;
			num = std::min(req->get_max_num_requests(interval),
				       max_requests_per_source);
			num = std::min(num, remaining);

			inter.total_length += num * req->get_request_length();
			inter.count        += num;
			remaining -= num;
		}
	}

	return inter;
}

struct ClusterLimit
{
	unsigned int max_total_requests;
	unsigned int max_requests_per_source;

	ClusterLimit(unsigned int total, unsigned int src) :
		max_total_requests(total), max_requests_per_source(src) {}
};

typedef std::vector<ClusterLimit> ClusterLimits;

static Interference bound_blocking_all_clusters(
	const ClusterResources& clusters,
	const ClusterLimits& limits,
	unsigned int res_id,
	unsigned long interval,
	const TaskInfo* exclude_tsk)
{
	Interference inter;
	unsigned int i;

	// add interference from each non-excluded cluster
	enumerate(clusters, it, i)
	{
		const Resources& resources = *it;
		const ClusterLimit& limit = limits[i];

		if (resources.size() > res_id)
			inter += bound_blocking(resources[res_id],
						interval,
						limit.max_total_requests,
						limit.max_requests_per_source,
						exclude_tsk);
	}

	return inter;
}

// Return a contention set that includes the longest requests from all
// clusters subject to the specified constraints.
static LimitedContentionSet contention_from_all_clusters(
	const ClusterResources& clusters,
	const ClusterLimits& limits,
	unsigned int res_id,
	unsigned long interval,
	const TaskInfo* exclude_tsk)
{
	LimitedContentionSet lcs;
	unsigned int i;

	// add interference from each non-excluded cluster
	enumerate(clusters, it, i)
	{
		const Resources& resources = *it;
		const ClusterLimit& limit = limits[i];

		if (resources.size() > res_id)
			add_blocking(lcs, resources[res_id],
				     interval,
				     limit.max_total_requests,
				     limit.max_requests_per_source,
				     exclude_tsk);
	}

	return lcs;
}


static Interference max_local_request_span(const TaskInfo &tsk,
					   const TaskInfos &tasks,
					   const BlockingBounds& bounds)
{
	Interference span;
	unsigned int i = 0;

	enumerate(tasks, it, i)
	{
		const TaskInfo& t = *it;

		if (&t != &tsk)
		{
			// only consider local, lower-priority tasks
			if (t.get_cluster() == tsk.get_cluster() &&
			    t.get_priority() >= tsk.get_priority())
			{
				Interference b = bounds.get_max_request_span(i);
				span = std::max(span, bounds.get_max_request_span(i));
			}
		}
	}

	return span;
}

static void charge_arrival_blocking(const ResourceSharingInfo& info,
				    BlockingBounds& bounds)
{
	unsigned int i = 0;
	const TaskInfos& tasks = info.get_tasks();

	enumerate(tasks, it, i)
	{
		Interference inf = max_local_request_span(*it, tasks, bounds);
		bounds[i] += inf; // charge to total
		bounds.set_arrival_blocking(i, inf);
	}
}


// **** blocking term analysis ****

BlockingBounds* global_omlp_bounds(const ResourceSharingInfo& info,
				   unsigned int num_procs)
{
	// split every thing by resources, sort, and then start counting.
	Resources resources;

	split_by_resource(info, resources);
	sort_by_request_length(resources);

	unsigned int i;
	BlockingBounds* _results = new BlockingBounds(info);
	BlockingBounds& results = *_results;

	for (i = 0; i < info.get_tasks().size(); i++)
	{
		const TaskInfo& tsk  = info.get_tasks()[i];
		Interference bterm;

		foreach(tsk.get_requests(), jt)
		{
			const RequestBound& req = *jt;
			const ContentionSet& cs =
				resources[req.get_resource_id()];

			unsigned int num_sources = cs.size();
			unsigned long interval = tsk.get_response();
			unsigned long issued   = req.get_num_requests();


			unsigned int total_limit = (2 * num_procs - 1) * issued;
			// Derived in the dissertation: at most twice per request.
			unsigned int per_src_limit = 2 * issued;

			if (num_sources <= num_procs + 1) {
				// FIFO case: no job is ever skipped in the
				//  priority queue (since at most one job is in
				//  PQ at any time).
				// Lemma 15 in RTSS'10: at most one blocking
				// request per source per issued request.
				per_src_limit = issued;
				total_limit   = (num_sources - 1) * issued;
			}

			bterm += bound_blocking(cs,
						interval,
						total_limit,
						per_src_limit,
						&tsk);
		}

		results[i] = bterm;
	}

	return _results;
}


BlockingBounds* global_fmlp_bounds(const ResourceSharingInfo& info)
{
	// split every thing by resources, sort, and then start counting.
	Resources resources;

	split_by_resource(info, resources);
	sort_by_request_length(resources);


	unsigned int i;
	BlockingBounds* _results = new BlockingBounds(info);
	BlockingBounds& results = *_results;

	unsigned int num_tasks = info.get_tasks().size();

	for (i = 0; i < info.get_tasks().size(); i++)
	{
		const TaskInfo& tsk  = info.get_tasks()[i];
		Interference bterm;


		foreach(tsk.get_requests(), jt)
		{
			const RequestBound& req = *jt;
			const ContentionSet& cs =
				resources[req.get_resource_id()];

			unsigned long interval = tsk.get_response();
			unsigned long issued   = req.get_num_requests();

			// every other task may block once per request
			unsigned int total_limit = (num_tasks - 1) * issued;
			unsigned int per_src_limit = issued;

			bterm += bound_blocking(cs,
						interval,
						total_limit,
						per_src_limit,
						&tsk);
		}

		results[i] = bterm;
	}

	return _results;
}

static ClusterLimits np_fifo_limits(
	const TaskInfo& tsk, const ClusterResources& clusters,
	unsigned int procs_per_cluster,
	const unsigned int issued,
	int dedicated_irq)
{
	ClusterLimits limits;
	int idx;
	limits.reserve(clusters.size());
	enumerate(clusters, ct, idx)
	{
		unsigned int total, parallelism = procs_per_cluster;

		if (idx == dedicated_irq)
			parallelism--;

		if (parallelism && (int) tsk.get_cluster() == idx)
			parallelism--;

		// At most one blocking request per remote CPU in
		// cluster per request.
		total = issued * parallelism;
		limits.push_back(ClusterLimit(total, issued));
	}

	return limits;
}

static Interference np_fifo_per_resource(
	const TaskInfo& tsk, const ClusterResources& clusters,
	unsigned int procs_per_cluster,
	unsigned int res_id, unsigned int issued,
	int dedicated_irq = NO_CPU)
{
	const unsigned long interval = tsk.get_response();
	ClusterLimits limits = np_fifo_limits(tsk, clusters, procs_per_cluster,
					      issued, dedicated_irq);
	return bound_blocking_all_clusters(clusters,
					   limits,
					   res_id,
					   interval,
					  &tsk);
}

static LimitedContentionSet np_fifo_per_resource_contention(
	const TaskInfo& tsk, const ClusterResources& clusters,
	unsigned int procs_per_cluster,
	unsigned int res_id, unsigned int issued,
	int dedicated_irq = NO_CPU)
{
	const unsigned long interval = tsk.get_response();
	ClusterLimits limits = np_fifo_limits(tsk, clusters, procs_per_cluster,
					      issued, dedicated_irq);
	return contention_from_all_clusters(clusters,
					    limits,
					    res_id,
					    interval,
					   &tsk);
}


// assumption: lcs is sorted by request length
static Interference bound_blocking(const LimitedContentionSet &lcs, unsigned int max_total_requests)
{
	Interference inter;
	unsigned int remaining = max_total_requests;

	foreach(lcs, it)
	{
		const LimitedRequestBound &lreqb = *it;
		unsigned int num;

		if (!remaining)
			break;

		num = std::min(lreqb.limit, remaining);

		inter.total_length += num * lreqb.request_bound->get_request_length();
		inter.count        += num;
		remaining          -= num;
	}

	return inter;
}

BlockingBounds* part_omlp_bounds(const ResourceSharingInfo& info)
{
	// split everything by partition
	Clusters clusters;

	split_by_cluster(info, clusters);

	// split each partition by resource
	ClusterResources resources;

	split_by_resource(clusters, resources);

	// sort each contention set by request length
	sort_by_request_length(resources);

	// We need for each task the maximum request span.  We also need the
	// maximum direct blocking from remote partitions for each request. We
	// can determine both in one pass.

	unsigned int i;

	// direct blocking results
	BlockingBounds* _results = new BlockingBounds(info);
	BlockingBounds& results = *_results;

	for (i = 0; i < info.get_tasks().size(); i++)
	{
		const TaskInfo& tsk  = info.get_tasks()[i];
		Interference bterm;

		foreach(tsk.get_requests(), jt)
		{
			const RequestBound& req = *jt;

			Interference blocking;

			blocking = np_fifo_per_resource(
				tsk, resources, 1,
				req.get_resource_id(), req.get_num_requests());

			// add in blocking term
			bterm += blocking;

			// Keep track of maximum request span.
			// Is this already a single-issue request?
			if (req.get_num_requests() != 1)
				// nope, need to recompute
				blocking = np_fifo_per_resource(
					tsk, resources, 1,
					req.get_resource_id(), 1);

			// The span includes our own request.
			blocking.total_length += req.get_request_length();
			blocking.count        += 1;

			// Update max. request span.
			results.raise_request_span(i, blocking);
		}

		results[i] = bterm;
	}

	charge_arrival_blocking(info, results);

	return _results;
}


BlockingBounds* clustered_omlp_bounds(const ResourceSharingInfo& info,
				      unsigned int procs_per_cluster,
				      int dedicated_irq)
{
	// split everything by partition
	Clusters clusters;

	split_by_cluster(info, clusters);

	// split each partition by resource
	ClusterResources resources;

	split_by_resource(clusters, resources);

	// sort each contention set by request length
	sort_by_request_length(resources);

	// We need for each task the maximum request span.  We also need the
	// maximum direct blocking from remote partitions for each request. We
	// can determine both in one pass.

	unsigned int i;

	// direct blocking results
	BlockingBounds* _results = new BlockingBounds(info);
	BlockingBounds& results = *_results;

	for (i = 0; i < info.get_tasks().size(); i++)
	{
		const TaskInfo& tsk  = info.get_tasks()[i];

		Interference bterm;

		foreach(tsk.get_requests(), jt)
		{
			const RequestBound& req = *jt;
			Interference blocking;

			blocking = np_fifo_per_resource(
				tsk, resources, procs_per_cluster,
				req.get_resource_id(),
				req.get_num_requests(),
				dedicated_irq);

			// add in blocking term
			bterm += blocking;

			// Keep track of maximum request span.
			// Is this already a single-issue request?
			if (req.get_num_requests() != 1)
				blocking = np_fifo_per_resource(
					tsk, resources, procs_per_cluster,
					req.get_resource_id(), 1);

			// The span includes our own request.
			blocking.total_length += req.get_request_length();
			blocking.count        += 1;
			// Update max. request span.
			results.raise_request_span(i, blocking);
		}

		results[i] = bterm;
	}

	// This is the initial delay due to priority donation.
	charge_arrival_blocking(info, results);

	return _results;
}

BlockingBounds* clustered_kx_omlp_bounds(const ResourceSharingInfo& info,
					 const ReplicaInfo& replicaInfo,
					 unsigned int procs_per_cluster,
					 int dedicated_irq)
{
	// split everything by partition
	Clusters clusters;

	split_by_cluster(info, clusters);

	const unsigned int num_cpus = clusters.size() * procs_per_cluster -
	                              (dedicated_irq != NO_CPU ? 1 : 0);

	// split each partition by resource
	ClusterResources resources;

	split_by_resource(clusters, resources);

	// sort each contention set by request length
	sort_by_request_length(resources);

	unsigned int i;

	// direct blocking results
	BlockingBounds* _results = new BlockingBounds(info);
	BlockingBounds& results = *_results;

	for (i = 0; i < info.get_tasks().size(); i++)
	{
		const TaskInfo& tsk  = info.get_tasks()[i];

		Interference bterm;

		foreach(tsk.get_requests(), jt)
		{
			const RequestBound& req = *jt;

			unsigned int max_total_once;
			LimitedContentionSet lcs;
			Interference blocking;

			max_total_once = divide_with_ceil(num_cpus,
					replicaInfo[req.get_resource_id()]) - 1;

			lcs = np_fifo_per_resource_contention(
					tsk, resources, procs_per_cluster,
					req.get_resource_id(),
					req.get_num_requests(),
					dedicated_irq);
			sort_by_request_length(lcs);
			blocking = bound_blocking(lcs, max_total_once * req.get_num_requests());

			// add in blocking term
			bterm += blocking;

			// Keep track of maximum request span.
			// Is this already a single-issue request?
			if (req.get_num_requests() != 1)
			{
				lcs = np_fifo_per_resource_contention(
						tsk, resources, procs_per_cluster,
						req.get_resource_id(),
						1, dedicated_irq);
				sort_by_request_length(lcs);
				blocking = bound_blocking(lcs, max_total_once);
			}

			// The span includes our own request.
			blocking.total_length += req.get_request_length();
			blocking.count        += 1;
			// Update max. request span.
			results.raise_request_span(i, blocking);
		}

		results[i] = bterm;
	}

	// This is the initial delay due to priority donation.
	charge_arrival_blocking(info, results);

	return _results;
}


struct RWCount {
	unsigned int res_id;
	unsigned int num_reads;
	unsigned int num_writes;
	unsigned int rlength;
	unsigned int wlength;

	RWCount(unsigned int id) : res_id(id),
				   num_reads(0),
				   num_writes(0),
				   rlength(0),
				   wlength(0)
	{}
};

typedef std::vector<RWCount> RWCounts;

static void merge_rw_requests(const TaskInfo &tsk, RWCounts &counts)
{
	foreach(tsk.get_requests(), req)
	{
		unsigned int res_id = req->get_resource_id();

		while (counts.size() <= res_id)
			counts.push_back(RWCount(counts.size()));

		if (req->is_read())
		{
			counts[res_id].num_reads += req->get_num_requests();
			counts[res_id].rlength = req->get_request_length();
		}
		else
		{
			counts[res_id].num_writes += req->get_num_requests();
			counts[res_id].wlength = req->get_request_length();
		}
	}
}


static Interference pf_writer_fifo(
	const TaskInfo& tsk, const ClusterResources& writes,
	const unsigned int num_writes,
	const unsigned int num_reads,
	const unsigned int res_id,
	const unsigned int procs_per_cluster,
	const int dedicated_irq)
{
	const unsigned int per_src_wlimit = num_reads + num_writes;
	const unsigned long interval = tsk.get_response();
	ClusterLimits limits;
	int idx;

	limits.reserve(writes.size());
	enumerate(writes, ct, idx)
	{
		unsigned int total, parallelism = procs_per_cluster;

		if (idx == dedicated_irq)
			parallelism--;

		if (parallelism && (int) tsk.get_cluster() == idx)
			parallelism--;

		// At most one blocking request per remote CPU in
		// cluster per request.
		if (parallelism)
			total = num_reads + num_writes * parallelism;
		else
			// No interference from writers if we are hogging
			// the only available CPU.
			total = 0;

		limits.push_back(ClusterLimit(total, per_src_wlimit));
	}

	Interference blocking;
	blocking = bound_blocking_all_clusters(writes,
					       limits,
					       res_id,
					       interval,
					       &tsk);
	return blocking;

}

static Interference pf_reader_all(
	const TaskInfo& tsk,
	const Resources& all_reads,
	const unsigned int num_writes,
	const unsigned int num_wblock,
	const unsigned int num_reads,
	const unsigned int res_id,
	const unsigned int procs_per_cluster,
	const unsigned int num_procs)
{
	const unsigned long interval = tsk.get_response();
	Interference blocking;
	unsigned int rlimit = std::min(num_wblock + num_writes,
				   num_reads + num_writes * (num_procs - 1));
	blocking = bound_blocking(all_reads[res_id],
				  interval,
				  rlimit,
				  rlimit,
				  // exclude all if c == 1
				  procs_per_cluster == 1,
				  &tsk);
	return blocking;
}

BlockingBounds* clustered_rw_omlp_bounds(const ResourceSharingInfo& info,
					 unsigned int procs_per_cluster,
					 int dedicated_irq)
{
	// split everything by partition
	Clusters clusters;

	split_by_cluster(info, clusters);

	// split each partition by resource
	ClusterResources resources;

	split_by_resource(clusters, resources);

	// split all by resource
	Resources all_task_reqs, all_reads, __all_writes;
	split_by_resource(info, all_task_reqs);
	split_by_type(all_task_reqs, all_reads, __all_writes);

	// sort each contention set by request length
	sort_by_request_length(resources);
	sort_by_request_length(all_reads);

	// split by type --- sorted order is maintained
	ClusterResources __reads, writes;
	split_by_type(resources, __reads, writes);


	// We need for each task the maximum request span.  We also need the
	// maximum direct blocking from remote partitions for each request. We
	// can determine both in one pass.

	const unsigned int num_procs = procs_per_cluster * clusters.size();
	unsigned int i;

	// direct blocking results
	BlockingBounds* _results = new BlockingBounds(info);
	BlockingBounds& results = *_results;

	for (i = 0; i < info.get_tasks().size(); i++)
	{
		const TaskInfo& tsk  = info.get_tasks()[i];
		RWCounts rwcounts;
		Interference bterm;

		merge_rw_requests(tsk, rwcounts);

		foreach(rwcounts, jt)
		{
			const RWCount& rw = *jt;

			// skip placeholders
			if (!rw.num_reads && !rw.num_writes)
				continue;

			Interference wblocking,  rblocking;

			wblocking = pf_writer_fifo(tsk, writes, rw.num_writes,
						   rw.num_reads, rw.res_id,
						   procs_per_cluster,
						   dedicated_irq);

			rblocking = pf_reader_all(tsk, all_reads, rw.num_writes,
						  wblocking.count, rw.num_reads,
						  rw.res_id, procs_per_cluster,
						  num_procs);

			//**** SINGLE WRITE
			Interference rblocking_w1, wblocking_w1;

			// Keep track of maximum request span.
			// Is this already a single-issue request?
			if (rw.num_writes &&
			    (rw.num_writes != 1 || rw.num_reads != 0))
			{
				wblocking_w1 = pf_writer_fifo(tsk, writes, 1, 0,
							      rw.res_id, procs_per_cluster,
							      dedicated_irq);

				rblocking_w1 = pf_reader_all(
					tsk, all_reads, 1,
					wblocking_w1.count, 0,
					rw.res_id, procs_per_cluster,
					num_procs);
			}
			else if (rw.num_writes)
			{
				  wblocking_w1 = wblocking;
				  rblocking_w1 = rblocking;
			}
			// else: zero, nothing to do

			//**** SINGLE READ

			Interference rblocking_r1, wblocking_r1;


			if (rw.num_reads &&
			    (rw.num_reads != 1 || rw.num_writes != 0))
			{
				wblocking_r1 = pf_writer_fifo(tsk, writes, 0, 1,
							      rw.res_id, procs_per_cluster,
							      dedicated_irq);

				rblocking_r1 = pf_reader_all(
					tsk, all_reads, 0,
					wblocking_r1.count, 1,
					rw.res_id, procs_per_cluster,
					num_procs);
			}
			else if (rw.num_reads)
			{
				wblocking_r1 = wblocking;
				rblocking_r1 = rblocking;
			}

			// else: zero, nothing to do

			// The span includes our own request.
			if (rw.num_writes)
			{
				wblocking_w1.total_length += rw.wlength;
				wblocking_w1.count        += 1;
			}
			if (rw.num_reads)
			{
				rblocking_r1.total_length += rw.rlength;
				wblocking_r1.count        += 1;
			}

			// combine
			wblocking_w1 += rblocking_w1;
			wblocking_r1 += rblocking_r1;
			wblocking    += rblocking;

			results.raise_request_span(i, wblocking_w1);
			results.raise_request_span(i, wblocking_r1);
			bterm += wblocking;
		}
		results[i] = bterm;
	}

	// This is the initial delay due to priority donation.
	charge_arrival_blocking(info, results);

	return _results;
}


BlockingBounds* task_fair_mutex_bounds(const ResourceSharingInfo& info,
				       unsigned int procs_per_cluster,
				       int dedicated_irq)
{
	// These are structurally equivalent. Therefore, no need to reimplement
	// everything from scratch.
	return clustered_omlp_bounds(info, procs_per_cluster, dedicated_irq);
}


BlockingBounds* phase_fair_rw_bounds(const ResourceSharingInfo& info,
				     unsigned int procs_per_cluster,
				     int dedicated_irq)
{
	// These are structurally equivalent. Therefore, no need to reimplement
	// everything from scratch.
	return clustered_rw_omlp_bounds(info, procs_per_cluster, dedicated_irq);
}


static Interference bound_blocking_all(
	const TaskInfo* tsk,
	const ContentionSet& all_reqs, // presumed sorted, for all clusters/tasks
	const unsigned int max_remote_requests, // per cluster
	const unsigned int max_local_requests,  // local cluster
	const unsigned int max_requests,        // per task
	unsigned int max_total)                 // stop after counting max_total
{
	unsigned long interval = tsk->get_response();
	hashmap<unsigned long, unsigned int> task_counter(512);
	hashmap<unsigned long, unsigned int>::iterator tctr;
	hashmap<unsigned int, unsigned int> cluster_counter(64);
	hashmap<unsigned int, unsigned int>::iterator cctr;
	Interference inter;

	cluster_counter[tsk->get_cluster()] = max_local_requests;

	foreach(all_reqs, it)
	{
		const RequestBound* req = *it;
		const TaskInfo* t = req->get_task();
		unsigned long key = (unsigned long) t;
		unsigned int cluster = t->get_cluster();

		if (!max_total)
			// we are done
			break;

		if (t == tsk)
			// doesn't block itself
			continue;

		// make sure we have seen this task
		tctr = task_counter.find(key);
		if (tctr == task_counter.end())
		{
			task_counter[key] = max_requests;
			tctr = task_counter.find(key);
		}

		if (!tctr->second)
			continue;

		cctr = cluster_counter.find(cluster);
		if (cctr == cluster_counter.end())
		{
			cluster_counter[cluster] = max_remote_requests;
			cctr = cluster_counter.find(cluster);
		}

		if (!cctr->second)
			continue;

		unsigned int remaining;
		remaining = std::min(tctr->second, cctr->second);
		remaining = std::min(remaining, max_total);
		unsigned int num = std::min(req->get_max_num_requests(interval), remaining);

		inter.total_length += num * req->get_request_length();
		inter.count        += num;
		cctr->second -= num;
		tctr->second -= num;
		max_total    -= num;
	}

	return inter;
}


static Interference tf_reader_all(
	const TaskInfo& tsk,
	const Resources& all_reads,
	const unsigned int num_writes,
	const unsigned int num_wblock,
	const unsigned int num_reads,
	const unsigned int res_id,
	const unsigned int procs_per_cluster)
{
	Interference blocking;
	unsigned int num_reqs = num_reads + num_writes;
	unsigned int max_reader_phases = num_wblock + num_writes;
	unsigned int task_limit = std::min(max_reader_phases, num_reqs);

	return bound_blocking_all(
		&tsk, all_reads[res_id],
		num_reqs * procs_per_cluster,
		num_reqs * (procs_per_cluster - 1),
		task_limit,
		max_reader_phases);
}


BlockingBounds* task_fair_rw_bounds(const ResourceSharingInfo& info,
				    const ResourceSharingInfo& info_mtx,
				    unsigned int procs_per_cluster,
				    int dedicated_irq)
{
	// split everything by partition
	Clusters clusters, clusters_mtx;

	split_by_cluster(info, clusters);
	split_by_cluster(info_mtx, clusters_mtx);

	// split each partition by resource
	ClusterResources resources, resources_mtx;

	split_by_resource(clusters, resources);
	split_by_resource(clusters_mtx, resources_mtx);

	// split all by resource
	Resources all_task_reqs, all_reads, __all_writes;
	split_by_resource(info, all_task_reqs);
	split_by_type(all_task_reqs, all_reads, __all_writes);

	// sort each contention set by request length
	sort_by_request_length(resources);
	sort_by_request_length(resources_mtx);
	sort_by_request_length(all_reads);

	// split by type --- sorted order is maintained
	ClusterResources __reads, writes;
	split_by_type(resources, __reads, writes);


	// We need for each task the maximum request span.  We also need the
	// maximum direct blocking from remote partitions for each request. We
	// can determine both in one pass.

	unsigned int i;

	// direct blocking results
	BlockingBounds* _results = new BlockingBounds(info);
	BlockingBounds& results = *_results;

	for (i = 0; i < info.get_tasks().size(); i++)
	{
		const TaskInfo& tsk  = info.get_tasks()[i];
		RWCounts rwcounts;

		Interference bterm;

		merge_rw_requests(tsk, rwcounts);

		foreach(rwcounts, jt)
		{
			const RWCount& rw = *jt;

			// skip placeholders
			if (!rw.num_reads && !rw.num_writes)
				continue;


			// 1) treat it as a mutex as a baseline
			Interference mtx, mtx_1;

			mtx = np_fifo_per_resource(
				tsk, resources_mtx, procs_per_cluster, rw.res_id,
				rw.num_reads + rw.num_writes,
				dedicated_irq);

			if (rw.num_reads + rw.num_writes == 1)
				mtx_1 = mtx;
			else
				mtx_1 = np_fifo_per_resource(
					tsk, resources_mtx, procs_per_cluster,
					rw.res_id, 1, dedicated_irq);

			// The span includes our own request.
			mtx_1.total_length += std::max(rw.wlength, rw.rlength);
			mtx_1.count        += 1;

			// 2) apply real RW analysis
			Interference wblocking, wblocking_1;
			Interference rblocking, rblocking_r1, rblocking_w1;

			wblocking = np_fifo_per_resource(
				tsk, writes, procs_per_cluster, rw.res_id,
				rw.num_reads + rw.num_writes,
				dedicated_irq);
			wblocking_1 = np_fifo_per_resource(
				tsk, writes, procs_per_cluster, rw.res_id, 1,
				dedicated_irq);

			rblocking = tf_reader_all(
				tsk, all_reads, rw.num_writes, wblocking.count,
				rw.num_reads, rw.res_id, procs_per_cluster);

			if (rw.num_writes)
			{
				// single write
				rblocking_w1 = tf_reader_all(
					tsk, all_reads, 1, wblocking.count,
					0, rw.res_id, procs_per_cluster);
				// The span includes our own request.
				rblocking_w1.total_length += rw.wlength;
				rblocking_w1.count        += 1;
			}
			if (rw.num_reads)
			{
				// single read
				rblocking_r1 = tf_reader_all(
					tsk, all_reads, 0, wblocking.count,
					1, rw.res_id, procs_per_cluster);
				// The span includes our own request.
				rblocking_r1.total_length += rw.rlength;
				rblocking_r1.count        += 1;
			}

			// combine
			wblocking   += rblocking;
			wblocking_1 += std::max(rblocking_w1, rblocking_r1);

			bterm += std::min(wblocking, mtx);
			results.raise_request_span(i, std::min(wblocking_1, mtx_1));
		}
		results[i] = bterm;
	}

	// This is the initial delay due to priority donation.
	charge_arrival_blocking(info, results);

	return _results;
}


/* this analysis corresponds to the FMLP+ in the dissertation */

static void pfmlp_count_direct_blocking(const TaskInfo* tsk,
					const ClusterResources& resources,
					std::vector<Interference>& counts)
{
	unsigned int interval = tsk->get_response();


	// for each resource requested by tsk
	foreach(tsk->get_requests(), jt)
	{
		const RequestBound& req = *jt;
		unsigned long issued    = req.get_num_requests();
		unsigned int res_id     = req.get_resource_id();

		unsigned int i;

		// for each cluster
		for (i = 0; i < resources.size(); i++)
		{
			// count interference... direct blocking will be counted later
			// make sure that cluster acceses res_id at  all
			if (resources[i].size() > res_id)
				// yes it does---how often can it block?
				counts[i] += bound_blocking(resources[i][res_id],
							    interval,
							    UNLIMITED,  // no total limit
							    issued, // once per request
							    tsk);
		}
	}
}

typedef std::vector<unsigned int> AccessCounts;
typedef std::vector<AccessCounts> PerClusterAccessCounts;

// How many times does a task issue requests that can
// conflict with tasks in a remote cluster. Indexed by cluster id.
typedef std::vector<unsigned int> IssuedRequests;
// Issued requests for each task. Indexed by task id.
typedef std::vector<IssuedRequests> PerTaskIssuedCounts;

static void derive_access_counts(const ContentionSet &cluster_contention,
				 AccessCounts &counts)
{
	foreach(cluster_contention, it)
	{
		const RequestBound *req = *it;
		unsigned int res_id = req->get_resource_id();

		while (counts.size() <= res_id)
			counts.push_back(0);

		counts[res_id] += req->get_num_requests();
	}
}

static void count_accesses_for_task(const TaskInfo& tsk,
				    const PerClusterAccessCounts& acc_counts,
				    IssuedRequests& ireqs)
{
	foreach(acc_counts, it)
	{
		const AccessCounts &ac = *it;
		unsigned int count = 0;

		// Check for each request of the task to see
		// if it conflicts with the cluster.
		foreach(tsk.get_requests(), jt)
		{
			const RequestBound &req = *jt;
			unsigned int res_id = req.get_resource_id();
			if (ac.size() > res_id && ac[res_id] > 0)
			{
				// cluster acceses res_id as well
				count += req.get_num_requests();
			}
		}
		ireqs.push_back(count);
	}
}

static void derive_access_counts(const AllPerCluster &per_cluster,
				 const ResourceSharingInfo &info,
				 PerTaskIssuedCounts &issued_reqs)
{
	PerClusterAccessCounts counts;

	/* which resources are accessed by each cluster? */
	map_ref(per_cluster, counts, AccessCounts, derive_access_counts);

	issued_reqs.reserve(info.get_tasks().size());

	foreach(info.get_tasks(), it)
	{
		issued_reqs.push_back(IssuedRequests());
		count_accesses_for_task(*it, counts, issued_reqs.back());
	}
}

static Interference pfmlp_bound_remote_blocking(const TaskInfo* tsk,
						const IssuedRequests &icounts,
						const std::vector<Interference>& counts,
						const ClusterContention& contention)
{
	unsigned int i;

	unsigned long interval = tsk->get_response();
	Interference blocking;

	// for each cluster
	for (i = 0; i < contention.size(); i++)
	{
		// Each task can either directly or indirectly block tsk
		// each time that tsk is directly blocked, but no more than
		// once per request issued by tsk.
		unsigned int max_per_task = std::min(counts[i].count, icounts[i]);

		// skip local cluster and independent clusters
		if (i == tsk->get_cluster() || !max_per_task)
			continue;

		Interference b;

		// for each task in cluster
		foreach(contention[i], it)
		{

			// count longest critical sections
			b += bound_blocking(*it,
					    interval,
					    max_per_task,
					    UNLIMITED, // no limit per source
					    tsk);
		}

		blocking += b;
	}
	return blocking;
}

static Interference pfmlp_bound_np_blocking(const TaskInfo* tsk,
					    const std::vector<Interference>& counts,
					    const AllPerCluster& per_cluster)
{
	unsigned int i;

	unsigned long interval = tsk->get_response();
	Interference blocking;

	// for each cluster
	for (i = 0; i < per_cluster.size(); i++)
	{
		// skip local cluster, this is only remote
		if (i == tsk->get_cluster())
			continue;

		// could be the same task each time tsk is directly blocked
		unsigned int max_direct = counts[i].count;
		Interference b;

		// count longest critical sections
		b += bound_blocking(per_cluster[i],
				    interval,
				    max_direct,
				    max_direct,
				    tsk);
		blocking += b;
	}
	return blocking;
}

static Interference pfmlp_bound_local_blocking(const TaskInfo* tsk,
					       const std::vector<Interference>& counts,
					       const ClusterContention& contention)
{
	// Locally, we have to account two things.
	// 1) Direct blocking from lower-priority tasks.
	// 2) Boost blocking from lower-priority tasks.
	// (Higher-priority requests are not counted as blocking.)
	// Since lower-priority jobs are boosted while
	// they directly block, 1) is subsumed by 2).
	// Lower-priority tasks cannot issue requests while a higher-priority
	// job executes. Therefore, at most one blocking request
	// is issued prior to the release of the job under analysis,
	// and one prior to each time that the job under analysis resumes.

	Interference blocking;
	Interference num_db = std::accumulate(counts.begin(), counts.end(),
					      Interference());
	unsigned int num_arrivals = std::min(tsk->get_num_arrivals(),
					     num_db.count + 1);
	unsigned long interval = tsk->get_response();

	const TaskContention& cont = contention[tsk->get_cluster()];

	// for each task in cluster
	foreach(cont, it)
	{
		// count longest critical sections
		blocking += bound_blocking(*it,
					   interval,
					   num_arrivals,
					   UNLIMITED, // no limit per source
					   tsk,
					   tsk->get_priority());
	}

	return blocking;
}

BlockingBounds* part_fmlp_bounds(const ResourceSharingInfo& info, bool preemptive)
{
	// split everything by partition
	Clusters clusters;

	split_by_cluster(info, clusters);

	// split each partition by resource
	ClusterResources resources;
	split_by_resource(clusters, resources);

	// find interference on a per-task basis
	ClusterContention contention;
	derive_task_contention(clusters, contention);

	// sort each contention set by request length
	sort_by_request_length(contention);

	// find total interference on a per-cluster basis
	AllPerCluster per_cluster;
	PerTaskIssuedCounts access_counts;

	all_per_cluster(clusters, per_cluster);
	sort_by_request_length(per_cluster);

	derive_access_counts(per_cluster, info, access_counts);

	// We need to find two blocking sources. Direct blocking (i.e., jobs
	// that are enqueued prior to the job under analysis) and boost
	// blocking, which occurs when the job under analysis is delayed
	// because some other job is priority-boosted. Boost blocking can be
	// local and transitive from remote CPUs. To compute this correctly,
	// we need to count how many times some job on a remote CPU can directly
	// block the job under analysis. So we first compute direct blocking
	// and count on which CPUs a job can be blocked.

	unsigned int i;

	// direct blocking results
	BlockingBounds* _results = new BlockingBounds(info);
	BlockingBounds& results = *_results;

	for (i = 0; i < info.get_tasks().size(); i++)
	{
		const TaskInfo& tsk  = info.get_tasks()[i];
		std::vector<Interference> counts(resources.size());
		Interference remote, local;

		// Determine counts.
		pfmlp_count_direct_blocking(&tsk, resources, counts);

		// Find longest remote requests.
		remote = pfmlp_bound_remote_blocking(&tsk, access_counts[i], counts,
							 contention);

		// Add in local boost blocking.
		local = pfmlp_bound_local_blocking(&tsk, counts, contention);

		if (!preemptive)
		{
			// Charge for additional delays due to remot non-preemptive
			// sections.
			remote += pfmlp_bound_np_blocking(&tsk, counts, per_cluster);
		}
		results[i] = remote + local;
		results.set_remote_blocking(i, remote);
		results.set_local_blocking(i, local);
	}

	return _results;
}

// ***************************  MPCP ******************************************


typedef std::vector<unsigned int> PriorityCeilings;

static void determine_priority_ceilings(const Resources& resources,
					PriorityCeilings& ceilings)
{
	ceilings.reserve(resources.size());

	foreach(resources, it)
	{
		unsigned int ceiling = UINT_MAX;
		const ContentionSet& cs = *it;

		foreach(cs, jt)
		{
			const RequestBound* req = *jt;
			ceiling = std::min(ceiling, req->get_task()->get_priority());
		}

		ceilings.push_back(ceiling);
	}
}

typedef std::vector<unsigned long> ResponseTimes;
typedef std::vector<ResponseTimes> TaskResponseTimes;
typedef std::vector<TaskResponseTimes> ClusterResponseTimes;

static unsigned long get_max_gcs_length(const TaskInfo* tsk,
					const PriorityCeilings& ceilings,
					unsigned int preempted_ceiling)
{
	unsigned long gcs_length = 0;

	foreach(tsk->get_requests(), it)
	{
		unsigned int prio  = ceilings[it->get_resource_id()];
		if (prio < preempted_ceiling)
			gcs_length = std::max(gcs_length,
					      (unsigned long) it->get_request_length());
	}

	return gcs_length;
}

static void determine_gcs_response_times(const TaskInfo* tsk,
					 const Cluster& cluster,
					 const PriorityCeilings& ceilings,
					 ResponseTimes& times)
{
	times.reserve(tsk->get_requests().size());

	foreach(tsk->get_requests(), it)
	{
		unsigned long resp = it->get_request_length();
		unsigned int prio  = ceilings[it->get_resource_id()];

		// Equation (2) in LNR:09.
		// One request of each local gcs that can preempt our ceiling,
		// but at most one per task (since tasks are sequential).

		foreach(cluster, jt)
		{
			const TaskInfo* t = *jt;

			if (t != tsk)
				resp += get_max_gcs_length(t, ceilings, prio);
		}

		times.push_back(resp);
	}
}

static void determine_gcs_response_times(const Cluster& cluster,
					 const PriorityCeilings& ceilings,
					 TaskResponseTimes& times)
{
	times.reserve(cluster.size());
	foreach(cluster, it)
	{
		times.push_back(ResponseTimes());
		determine_gcs_response_times(*it, cluster, ceilings,
					     times.back());
	}
}

static void determine_gcs_response_times(const Clusters& clusters,
					 const PriorityCeilings& ceilings,
					 ClusterResponseTimes& times)
{
	times.reserve(clusters.size());
	foreach(clusters, it)
	{
		times.push_back(TaskResponseTimes());
		determine_gcs_response_times(*it, ceilings, times.back());
	}
}

static unsigned long response_time_for(unsigned int res_id,
 				       unsigned long interval,
				       const TaskInfo* tsk,
				       const ResponseTimes& resp,
				       bool multiple)
{
	const Requests& requests = tsk->get_requests();
	unsigned int i = 0;

	for (i = 0; i < requests.size(); i++)
		if (requests[i].get_resource_id() == res_id)
		{
			if (multiple)
			{
				// Equation (3) in LNR:09.
				// How many jobs?
				unsigned long num_jobs;
				num_jobs  = divide_with_ceil(interval, tsk->get_period());
				num_jobs += 1;

				// Note: this may represent multiple gcs, so multiply.
				return num_jobs * resp[i] * requests[i].get_num_requests();
			}
			else
				// Just one request.
				return resp[i];
		}
	// if we get here, then the task does not access res_id
	return 0;
}

static unsigned long  mpcp_remote_blocking(unsigned int res_id,
					   unsigned long interval,
					   const TaskInfo* tsk,
					   const Cluster& cluster,
					   const TaskResponseTimes times,
					   unsigned long& max_lower)
{
	unsigned int i;
	unsigned long blocking = 0;

	// consider each task in cluster
	for (i = 0; i < cluster.size(); i++)
	{
		const TaskInfo* t = cluster[i];
		if (t != tsk)
		{
			if (t->get_priority() < tsk->get_priority())
				// This is a higher-priority task;
				// it can block multiple times.
				blocking += response_time_for(res_id, interval,
							      t, times[i], true);
			else
				// This is a lower-priority task;
				// it can block only once.
				max_lower = std::max(max_lower,
						     response_time_for(res_id, interval,
								       t, times[i], false));
		}
	}

	return blocking;
}

static unsigned long  mpcp_remote_blocking(unsigned int res_id,
					   unsigned long interval,
					   const TaskInfo* tsk,
					   const Clusters& clusters,
					   const ClusterResponseTimes times,
					   unsigned long& max_lower)
{
	unsigned int i;
	unsigned long blocking;

	max_lower = 0;
	blocking  = 0;

	for (i = 0; i < clusters.size(); i++)
	{
		blocking += mpcp_remote_blocking(res_id, interval,
						 tsk, clusters[i], times[i],
						 max_lower);
	}
	return blocking;
}

static unsigned long mpcp_remote_blocking(unsigned int res_id,
					  const TaskInfo* tsk,
					  const Clusters& clusters,
					  const ClusterResponseTimes times)
{
	unsigned long interval;
	unsigned long blocking = 1;
	unsigned long max_lower;

	do
	{
		// last bound
		interval = blocking;
		// Bail out if it doesn't converge.
		if (interval > tsk->get_response())
			return UNLIMITED;

		blocking = mpcp_remote_blocking(res_id, interval,
						tsk, clusters, times,
						max_lower);

		// Account for the maximum lower-priority gcs
		// that could get in the way.
		blocking += max_lower;

		// Loop until it converges.
	} while ( interval != blocking );

	return blocking;
}

static unsigned long mpcp_remote_blocking(const TaskInfo* tsk,
					  const Clusters& clusters,
					  const ClusterResponseTimes times)
{
	unsigned long blocking = 0;


	const Requests& requests = tsk->get_requests();
	unsigned int i = 0;

	for (i = 0; i < requests.size(); i++)
	{
		unsigned int b;
		b = mpcp_remote_blocking(requests[i].get_resource_id(),
					 tsk, clusters, times);
		if (b != UNLIMITED)
			// may represent multiple, multiply accordingly
			blocking += b * requests[i].get_num_requests();
		else
			// bail out if it didn't converge
			return b;
	}

	return blocking;
}

static unsigned long mpcp_arrival_blocking(const TaskInfo* tsk,
					   const Cluster& cluster,
					   bool virtual_spinning)
{
	unsigned int prio = tsk->get_priority();
	unsigned int blocking = 0;
	unsigned int i;

	for (i = 0; i < cluster.size(); i++)
		if (cluster[i] != tsk && cluster[i]->get_priority() >= prio)
			blocking += cluster[i]->get_max_request_length();

	if (virtual_spinning)
		// Equation (4) in LNR:09.
		return blocking;
	else
		// Equation (1) in LNR:09.
		return blocking * tsk->get_num_arrivals();
}

BlockingBounds* mpcp_bounds(const ResourceSharingInfo& info,
			    bool use_virtual_spinning)
{
	Resources resources;
	Clusters clusters;

	split_by_resource(info, resources);
	split_by_cluster(info, clusters);

	// 2) Determine priority ceiling for each request.
	PriorityCeilings gc;
	determine_priority_ceilings(resources, gc);


	// 3) For each request, determine response time. This only depends on the
	//    priority ceiling for each request.
	ClusterResponseTimes responses;
	determine_gcs_response_times(clusters, gc, responses);

	unsigned int i;

	BlockingBounds* _results = new BlockingBounds(info);
	BlockingBounds& results = *_results;

	for (i = 0; i < info.get_tasks().size(); i++)
	{
		const TaskInfo& tsk  = info.get_tasks()[i];

		unsigned long remote, local = 0;

		// 4) Determine remote blocking for each request. This depends on the
		//    response times for each remote request.
		remote = mpcp_remote_blocking(&tsk, clusters, responses);

		// 5) Determine arrival blocking for each task.
		if (remote != UNLIMITED)
			local = mpcp_arrival_blocking(&tsk, clusters[tsk.get_cluster()],
						      use_virtual_spinning);

		// 6) Sum up blocking: remote blocking + arrival blocking.
		results[i].total_length = remote + local;


		Interference inf;
		inf.total_length = remote;
		results.set_remote_blocking(i, inf);
	}

	return _results;
}


// ************************************************** DPCP **************
/*

  DPCP blocking terms (Rajkumar, 1991, page 87):

  1) Local PCP blocking => does not apply here, we only care about global
  resources.

  2) A lower-priority gcs on a remote proc each time that Ji issues a request.

  3) All requests of all higher-priority tasks on all remote processors that Ji
  accesses.

  4) Global critical sections on Ji's CPU. Since gcs are not part of the job
  execution time in our model, it does not matter whether the local gcs's
  belong to lower or higher-priority tasks.
 */


static void split_by_locality(const ResourceSharingInfo& info,
			      const ResourceLocality& locality,
			      AllPerCluster& per_cluster)
{
	foreach(info.get_tasks(), it)
	{
		while (it->get_cluster()  >= per_cluster.size())
			per_cluster.push_back(ContentionSet());

		foreach(it->get_requests(), jt)
		{
			const RequestBound &req = *jt;
			int cpu = locality[req.get_resource_id()];

			if (cpu == NO_CPU)
				// NO_CPU => dedicated synchronization processor
				continue;

			while ((unsigned int) cpu  >= per_cluster.size())
				per_cluster.push_back(ContentionSet());

			per_cluster[cpu].push_back(&req);
		}
	}
}

static unsigned int count_requests_to_cpu(
	const TaskInfo& tsk,
	const ResourceLocality& locality,
	int cpu)
{
	unsigned int count = 0;

	foreach(tsk.get_requests(), req)
		if (locality[req->get_resource_id()] == cpu)
			count += req->get_num_requests();

	return count;
}

static Interference bound_blocking_dpcp(
	const TaskInfo* tsk,
	const ContentionSet& cont,
	unsigned int max_lower_prio)
{
	Interference inter;
	const unsigned int interval = tsk->get_response();

	// assumption: cont is ordered by request length
	foreach(cont, it)
	{
		const RequestBound* req = *it;

		// can't block itself
		if (req->get_task() != tsk)
		{
			unsigned int num;
			if (req->get_task()->get_priority() < tsk->get_priority())
			{
				// higher prio => all of them
				num = req->get_max_num_requests(interval);
				inter.count += num;
				inter.total_length += num * req->get_request_length();
			}
			else if (max_lower_prio)
			{
				// lower prio => only remaining
				num = std::min(req->get_max_num_requests(interval), max_lower_prio);
				inter.count += num;
				inter.total_length += num * req->get_request_length();
				max_lower_prio -= num;
			}
		}
	}

	return inter;
}

static Interference dpcp_remote_bound(
	const TaskInfo& tsk,
	const ResourceLocality& locality,
	const AllPerCluster& per_cpu)
{
	Interference blocking;
	unsigned int cpu = 0;

	foreach(per_cpu, it)
	{
		// this is about remote delays
		if (cpu != tsk.get_cluster())
		{
			const ContentionSet &cs = *it;
			unsigned int reqs;
			reqs = count_requests_to_cpu(tsk, locality, cpu);

			if (reqs > 0)
				blocking += bound_blocking_dpcp(&tsk, cs, reqs);
		}
		cpu++;
	}

	return blocking;
}


static Interference dpcp_local_bound(
	const TaskInfo* tsk,
	const ContentionSet& local)
{
	Interference blocking;
	const unsigned int interval = tsk->get_response();

	foreach(local, it)
	{
		const RequestBound* req = *it;
		if (req->get_task() != tsk)
		{
			unsigned int num;
			num = req->get_max_num_requests(interval);
			blocking.count += num;
			blocking.total_length += num * req->get_request_length();
		}
	}

	return blocking;
}


BlockingBounds* dpcp_bounds(const ResourceSharingInfo& info,
			    const ResourceLocality& locality)
{
	AllPerCluster per_cpu;

	split_by_locality(info, locality, per_cpu);
	sort_by_request_length(per_cpu);

	BlockingBounds* _results = new BlockingBounds(info);
	BlockingBounds& results = *_results;

	for (unsigned int i = 0; i < info.get_tasks().size(); i++)
	{
		const TaskInfo& tsk  = info.get_tasks()[i];
		Interference remote, local;

		remote = dpcp_remote_bound(tsk, locality, per_cpu);
		local = dpcp_local_bound(&tsk, per_cpu[tsk.get_cluster()]);

		results[i] = remote + local;
		results.set_remote_blocking(i, remote);
		results.set_local_blocking(i, local);
	}
	return _results;
}

