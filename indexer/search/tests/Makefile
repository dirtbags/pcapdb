all:

test: gen_test_index.py
	time python3 gen_test_index.py
	time ./test.sh

clean:
	rm -f *results_py
	rm -f *results_c
	rm -f test_flow test_ipv4 test_ipv6 test_port
	rm -f test.sh
