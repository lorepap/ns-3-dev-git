#!/bin/bash
PROT=mixed
OUTPUT=$PROT-red-queue-evaluation
./ns3 run scratch/mixed-tcp-test --command-template="%s --protocol=$PROT --filename=$OUTPUT"