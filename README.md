#  OS Jackfruit – Mini Container Runtime with Memory Monitoring
##  Team Information

| Name            | SRN             |
|-----------------|-----------------|
| Kavya P Korti   | PES1UG24CS575   |
| Arati           | PES1UG24CS558   |

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
<img width="1366" height="768" alt="WhatsApp Image 2026-04-21 at 11 05 07 PM" src="https://github.com/user-attachments/assets/f26f5b5b-eaa1-4acd-a0c0-5003ce768248" />



```bash
make
```

---

### 2. Kernel Module Loaded

<img width="1879" height="111" alt="image" src="https://github.com/user-attachments/assets/12bc8684-a89b-4ecf-9a13-88db40aa1a3b" />


```bash
sudo insmod monitor.ko
ls -l /dev/container_monitor
```

---

###  3. Supervisor Running

<img width="1206" height="90" alt="image" src="https://github.com/user-attachments/assets/305ef686-0ccd-40d3-9426-70f3037188f0" />


```bash
sudo ./engine supervisor ./rootfs-base
```

---



### 4. Container Status

<img width="604" height="95" alt="image" src="https://github.com/user-attachments/assets/94855922-9929-4d7e-aed3-56e4319d10d5" />


```bash
sudo ./engine ps
```

---

### 5. CPU Logs
<img width="742" height="247" alt="image" src="https://github.com/user-attachments/assets/471655ac-c0a2-4fd1-b004-d583a4abf1d4" />


```bash
sudo ./engine logs alpha
```

---

### 6. IO Logs

<img width="716" height="467" alt="image" src="https://github.com/user-attachments/assets/0641358b-e801-419b-b8d6-2d284f55579d" />

```bash
sudo ./engine logs beta
```

---

### 7. Memory Container Start

<img width="695" height="60" alt="image" src="https://github.com/user-attachments/assets/6a0e740e-f03d-4a96-9e2c-091462e12313" />


```bash
sudo ./engine stop mem1 ...
```

---

### 8. Memory Limit Logs

<img width="1201" height="534" alt="image" src="https://github.com/user-attachments/assets/5c2973bf-d8eb-4172-97c8-4ab8ecc03a0d" />
<img width="1215" height="754" alt="image" src="https://github.com/user-attachments/assets/70a776b0-d1c5-4495-9af4-b63a9fb8a8ca" />



```bash
sudo dmesg | tail -n 30
```

---



###  9. Process Check

<img width="1206" height="90" alt="image" src="https://github.com/user-attachments/assets/2e42c125-ab40-47f2-8f1b-1dc7dcb93292" />


```bash
ps aux | grep defunct
```

---

### 10. Logs Directory
<img width="745" height="142" alt="image" src="https://github.com/user-attachments/assets/4f028c0b-ec02-4bca-9c49-cccab934d5d8" />


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
