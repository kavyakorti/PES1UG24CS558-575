#  OS Jackfruit – Mini Container Runtime with Memory Monitoring

##  Overview

This project implements a **mini container runtime in C** along with a **Linux kernel module** to monitor and enforce memory usage limits.

It supports:

* Running isolated containers using root filesystem
* Executing CPU, IO, and Memory workloads
* Logging container outputs
* Enforcing soft and hard memory limits

---

##  Components

###  User-space Runtime

* `engine.c` → Main runtime
* Commands:

  * `start`
  * `stop`
  * `ps`
  * `logs`

###  Kernel Module

* `monitor.c` → Memory monitoring
* `monitor_ioctl.h` → Interface
* Features:

  * Soft limit detection
  * Hard limit enforcement (SIGKILL)

### Workload Programs

* `cpu_hog.c` → CPU intensive
* `io_pulse.c` → IO simulation
* `memory_hog.c` → Memory allocation

---

##  Project Structure

```
OS-Jackfruit/
│
├── boilerplate/
│   ├── engine.c
│   ├── monitor.c
│   ├── monitor_ioctl.h
│   ├── cpu_hog.c
│   ├── io_pulse.c
│   ├── memory_hog.c
│   ├── Makefile
│   ├── rootfs-base/
│   ├── rootfs-alpha/
│   ├── rootfs-beta/
│   └── logs/
```

---

## Setup Instructions

### 1. Clone Repository

```bash
git clone https://github.com/YOUR-USERNAME/OS-Jackfruit.git
cd OS-Jackfruit/boilerplate
```

---

### 2. Build Project

```bash
make
```

---

### 3. Load Kernel Module

```bash
sudo insmod monitor.ko
ls -l /dev/container_monitor
```

---

### 4. Setup Root Filesystem

```bash
mkdir -p rootfs-base
wget https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.3-x86_64.tar.gz
tar -xzf alpine-minirootfs-3.20.3-x86_64.tar.gz -C rootfs-base

cp -a rootfs-base rootfs-alpha
cp -a rootfs-base rootfs-beta
```

---

### 5. Build Static Binaries

```bash
sudo apt install -y musl-tools

musl-gcc -static -O2 -o cpu_hog cpu_hog.c
musl-gcc -static -O2 -o io_pulse io_pulse.c
musl-gcc -static -O2 -o memory_hog memory_hog.c
```

---

### 6. Copy Binaries

```bash
cp cpu_hog rootfs-alpha/
cp io_pulse rootfs-beta/
cp memory_hog rootfs-alpha/
cp memory_hog rootfs-beta/

chmod +x rootfs-alpha/* rootfs-beta/*
```

---

##  Execution

### Terminal 1 – Start Supervisor

```bash
sudo ./engine supervisor ./rootfs-base
```

---

### Terminal 2 – Run Containers

```bash
sudo ./engine start alpha ./rootfs-alpha /cpu_hog --soft-mib 48 --hard-mib 80 --nice 5
sudo ./engine start beta ./rootfs-beta /io_pulse --soft-mib 64 --hard-mib 96 --nice 0
```

---

### Check Status

```bash
sudo ./engine ps
```

---

### View Logs

```bash
sudo ./engine logs alpha
sudo ./engine logs beta
```

---

### Memory Test

```bash
sudo ./engine start mem1 ./rootfs-alpha /memory_hog --soft-mib 20 --hard-mib 35
sudo ./engine ps
sudo dmesg | tail -n 30
```

---

## 📸 Screenshots

###  1. Build Process

📸 Insert screenshot of:

```bash
make
```

---

### 2. Kernel Module Loaded

📸 Insert screenshot of:

```bash
sudo insmod monitor.ko
ls -l /dev/container_monitor
```

---

###  3. Supervisor Running

📸 Insert screenshot of:

```bash
sudo ./engine supervisor ./rootfs-base
```

---

###  4. Starting Containers

📸 Insert screenshot of:

```bash
sudo ./engine start alpha ...
sudo ./engine start beta ...
```

---

### 5. Container Status

📸 Insert screenshot of:

```bash
sudo ./engine ps
```

---

### 6. CPU Logs

📸 Insert screenshot of:

```bash
sudo ./engine logs alpha
```

---

### 7. IO Logs

📸 Insert screenshot of:

```bash
sudo ./engine logs beta
```

---

### 8. Memory Container Start

📸 Insert screenshot of:

```bash
sudo ./engine start mem1 ...
```

---

### 9. Memory Limit Logs

📸 Insert screenshot of:

```bash
sudo dmesg | tail -n 30
```

---

### 10. Final Container Status

📸 Insert screenshot of:

```bash
sudo ./engine ps
```

---

###  11. Process Check

📸 Insert screenshot of:

```bash
ps aux | grep defunct
```

---

### 12. Logs Directory

📸 Insert screenshot of:

```bash
ls -l logs
```

---

##  Cleanup

```bash
sudo ./engine stop alpha
sudo ./engine stop beta
sudo ./engine stop mem1

sudo rmmod monitor
```

---



##  Conclusion

This project demonstrates:

* Container runtime implementation in user space
* Kernel module integration
* Memory monitoring and enforcement
* Process isolation and logging

---
