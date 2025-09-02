# Databases-labs-WSDB

This repository contains my coursework projects for the **Introduction to Databases** course at Nanjing University.  
It is based on the [NJU_DBPractice framework](https://github.com/nju-websoft/NJU_DBPractice), with my own implementations for buffer management and query execution.

## 📂 Labs Overview

### Lab 1: Storage and Buffer Management
- Implemented **LRU Replacer** for page replacement
- Implemented **Buffer Pool Manager** for page fetching, pin/unpin, flush, and eviction
- Designed **Table Handle** for record-level operations (insert/delete)
- Extended with:
  - **LRU-K Replacer**: improved replacement policy
  - **PAX Page Handle**: optimized storage model

### Lab 2: Query Execution Engine
- Implemented basic **executors**:
  - Insert, Delete, Update
  - SeqScan, Projection, Filter, Limit
  - Sort (with in-memory & external merge-sort)
- Covered core concepts of relational query execution pipelines

## 🛠️ Tech Stack
- C++  
- Flex & Bison (for SQL parsing)  
- Data structures & algorithms for query execution  

## 📖 Learning Outcome
- Gained hands-on experience in database internals  
- Learned how SQL queries are parsed, optimized, and executed  
- Built a working prototype of a mini relational DBMS

## 📖 Documentation
Detailed lab reports are provided in the repo:  
- [Lab1 Report](lab1_205220025_郑凯琳.pdf)  
- [Lab2 Report](lab2_205220025_郑凯琳.pdf)

## 🚀 How to Build & Run
1. Clone this repo
2. Follow the build instructions in the [original framework](https://github.com/nju-websoft/NJU_DBPractice)
3. Replace `src/` components with my implementations
4. Run the provided test cases
