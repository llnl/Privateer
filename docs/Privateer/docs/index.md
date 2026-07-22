# Welcome to MkDocs

This Read the Docs page describes Privateer (open-source library available [here]((https://github.com/LLNL/privateer))).

# Overview

Privateer is a general-purpose data store that optimizes the tradeoff between storage space utilization and I/O performance. 
Privateer uses memory-mapped I/O with private mapping and an optimized writeback mechanism to maximize write parallelism and 
eliminate redundant writes; it also uses content-addressable storage to optimize storage space via de-duplication.
