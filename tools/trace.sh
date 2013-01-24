#!/bin/bash

function trace_off {
    echo 0 > tracing_on
    echo 0 > tracing_enabled
}

function trace_on {
    echo 1 > tracing_on
    echo 1 > tracing_enabled
}

function trace {
    (cat trace_pipe > /tmp/trace &);
    sleep "${1:-5}"
}

pushd /sys/kernel/debug/tracing
#pushd /debug/tracing

trace_off

#echo 'hrtimer_*' > set_graph_function
echo 'tick_program_event' > set_graph_function
echo 'native_apic_mem_*' >> set_graph_function

echo function_graph > current_tracer

trace_on
trace 5
trace_off

popd

