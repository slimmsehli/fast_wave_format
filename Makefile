

clean:
	rm -rf parser wave *.wave newsim parser_varints parser_masarray

comp_masarray:
	g++ -std=c++20 -O3 -Wall parser_masarray.cpp -o parser_masarray 

run:
	./parser_masarray sim.vcd sim_masarray.wave

comp_varints:
	g++ -std=c++20 -O3 -Wall parser_varints.cpp -o parser_varints 

run_varints:
	./parser_varints sim.vcd sim_varints.wave


comp_read_fwf:
	g++ -std=c++20 -O3 -Wall reader_fwf.cpp -o reader_fwf 

run_read_fwf:
	time ./reader_fwf sim_varints.wave


comp_read_vcd:
	g++ -std=c++20 -O3 -Wall reader_vcd.cpp -o reader_vcd

run_read_vcd:
	time ./reader_vcd sim.vcd