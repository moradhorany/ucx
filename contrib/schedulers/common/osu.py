#!/usr/bin/python

# TODO: Test only PPN=1 and PPN=FULL (no need to pass anything! :)

import os, sys, subprocess, csv, fcntl

OSU_DONE_MARK   = "1048576" # last datapoint...
MPIRUN_PATH     = "bin/mpirun"
OSU_PATH        = "libexec/osu-micro-benchmarks/mpi/collective/"
OSU_TESTS       = ["osu_barrier  ",
                   "osu_bcast    ",
                   "osu_reduce   ",
                   "osu_allreduce"]
OSU_COLL_LIBS   = {"ucx"     : "          ",
                   "non_ucx" : "^ucx      ",
                   "naive"   : "^ucx,hcoll"}
UCX_THRESHOLDS  = {"_short_only" : "UCX_BUILTIN_SHORT_MAX_TX_SIZE=inf UCX_BUILTIN_BCOPY_MAX_TX_SIZE=inf",
                   "_bcopy_only" : "UCX_BUILTIN_SHORT_MAX_TX_SIZE=0   UCX_BUILTIN_BCOPY_MAX_TX_SIZE=inf",
                   "_zcopy_only" : "UCX_BUILTIN_SHORT_MAX_TX_SIZE=0   UCX_BUILTIN_BCOPY_MAX_TX_SIZE=0  "}
OSU_CMD_SLURM   = "OMPI_MCA_coll={lib} {env} /usr/bin/srun --nodes={node_cnt} --ntasks-per-node={ppn}      {test} -f"
OSU_CMD_DEFAULT = "{mpirun} -mca coll {lib} {env} --display-map --map-by core --bind-to core -H {hostlist} {test} -f"
RESULT_FOLDER   = "results"
OUTPUT_FILE     = RESULT_FOLDER + "/{test}_n{hosts}_ppn{ppn}_{lib}"
CSV_FILE        = "osu_results.csv"
CSV_FORMAT      = "Collective Type|Host Count|Ranks Per Host|Collectives Library|Message Size|Avg Latency (us)|Min Latency (us)|Max Latency (us)|Iterations".split("|")

def mpi_path(base_path):
	return os.path.join(base_path, MPIRUN_PATH)

def osu_path(base_path, osu_test):
	return os.path.join(base_path, OSU_PATH, osu_test)

def test_path(base_path):
	return (os.path.exists(mpi_path(base_path)) and
		os.path.exists(osu_path(base_path, OSU_TESTS[0]).strip()))

def gen_single_osu_cmd(base_path, osu_test, node_cnt, ppn, partial_list, lib_name, lib_cmd, env_name, env_cmd, is_slurm):
	if is_slurm:
		base_cmd = OSU_CMD_SLURM
	else:
		base_cmd = OSU_CMD_DEFAULT
		env_cmd = "-x " + " -x ".join(env_cmd.split())
	cmd = base_cmd.format(mpirun=mpi_path(base_path),
	                      test=osu_path(base_path, osu_test),
	                      node_cnt=node_cnt,
	                      ppn=ppn,
	                      hostlist=partial_list,
	                      env=env_cmd,
	                      lib=lib_cmd)

	out = OUTPUT_FILE.format(test=osu_test.strip(),
	                         hosts=node_cnt,
	                         ppn=ppn,
	                         lib=lib_name + env_name)
	return cmd, out

def gen_osu_cmds(node_list, max_ppn, base_path, is_slurm=False, check_threshold=True):
	cmd_list = []
	nodes_total = node_list.count(",") + 1
	for node_cnt in [2**(i+1) for i in range(nodes_total) if 2**i <= nodes_total]:
		for ppn in [1, max_ppn]:
			seperator = ":%i," % ppn
			partial_list = seperator.join(node_list.split(",")[:node_cnt]) + seperator[:-1]
			for osu_test in OSU_TESTS:
				for lib_name, lib_cmd in OSU_COLL_LIBS.items():
					cmd, out = gen_single_osu_cmd(base_path, osu_test, node_cnt,
						ppn, partial_list, lib_name, lib_cmd, "", "", is_slurm)
					cmd_list.append((cmd, os.path.join(base_path, out)))

					if lib_name == "ucx" and check_threshold:
						for env_name, env_cmd in UCX_THRESHOLDS.items():
							cmd, out = gen_single_osu_cmd(base_path, osu_test, node_cnt,
								ppn, partial_list, lib_name, lib_cmd, env_name, env_cmd, is_slurm)
							cmd_list.append((cmd, os.path.join(base_path, out)))
	return cmd_list

def execute(command, output_path, timeout_seconds):
	print(command)
	print(output_path)
	out_file = output_path + ".out"
	err_file = output_path + ".err"
	if os.path.exists(out_file) and OSU_DONE_MARK in open(out_file).read():
		print("Skipping.")
	else:
		print ("Executing...")

	with open(out_file, 'w') as out:
		fcntl.flock(out, fcntl.LOCK_EX | fcntl.LOCK_NB)
		out.write(command)
		out.write("\n\n")
		out.flush()
		with open(err_file, 'w') as err:
			if timeout_seconds:
				subprocess.call(command, stdout=out, stderr=err, shell=True, timeout=timeout_seconds)
			else:
				subprocess.call(command, stdout=out, stderr=err, shell=True)

	if os.path.exists(err_file) and os.path.getsize(err_file) == 0:
		os.remove(err_file)

def parse_path(results_path):
	with open(CSV_FILE, "w") as csvfile:
		osu_writer = csv.writer(csvfile)
		osu_writer.writerow(CSV_FORMAT)
		for file_name in os.listdir(results_path):
			file_path = os.path.join(results_path, file_name)
			if not os.path.isfile(file_path):
				continue
			try:
				osu, coll, host_cnt, ppn, test = file_name.split("_") # e.g. osu_reduce_n2_ppn4_ucx.out
				assert(osu == "osu")
				assert(test.endswith(".out"))
				test = test[:-4]
				host_cnt = int(host_cnt[1:])
				ppn = int(ppn[3:])

				with open(file_path) as result:
					for line in result.readlines():
						if not line[0].isdigit():
							continue
						try:
							size, avg, min, max, iters = map(float, line.split())
							size = int(size)
							osu_writer.writerow((coll, host_cnt, ppn, test, int(size), avg, min, max, int(iters)))
						except:
							continue
			except:
				continue

if __name__ == "__main__":
	if len(sys.argv) < 2 or len(sys.argv) > 5:
		print("USAGE (#1 for parsing, #2 for execution):")
		print("1. osu.py <path_to_results>")
		print("2. osu.py <comma_seperated_node_list> <max_ppn> [[<build_path>] <timeout_in_seconds*>]")
		print(" *in Python 3.3+")
		sys.exit(1)

	if len(sys.argv) == 2:
		parse_path(sys.argv[1])
	else:
		host_list = sys.argv[1]
		max_ppn   = int(sys.argv[2])
		base_path = "."
		timeout   = None

		if len(sys.argv) > 3:
			base_path = sys.argv[3]
		if len(sys.argv) > 4:
			timeout = int(sys.argv[4])

		# Make sure the path contains OMPI and OSU
		if not test_path(base_path):
			print("Couldn't find {} or {}".format(mpi_path(base_path), osu_path(base_path, OSU_TESTS[0])))
			sys.exit(1)

		# Create results folder
		folder = os.path.join(base_path, RESULT_FOLDER)
		if not os.path.exists(folder):
			os.mkdir(folder)

		# Generate command lines
		commands = gen_osu_cmds(host_list, max_ppn, base_path, True) # TODO: SLURM auto-detection!
		for cmd, out in commands:
			try:
				execute(cmd, out, timeout)
			except:
				print("Unexpected error:", sys.exc_info()[0])
