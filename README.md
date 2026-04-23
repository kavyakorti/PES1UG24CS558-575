#  OS Jackfruit – Mini Container Runtime with Memory Monitoring
##  Team Information

> **Team Members**

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

###  Workload Programs

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

##  Setup Instructions

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

##  Screenshots

###  1. Build Process

<img width="847" height="201" alt="image" src="https://github.com/user-attachments/assets/f25b1b73-e786-46e6-9d6f-8bb0aeff2f86" />


```bash
make
```

---

###  2. Kernel Module Loaded

<img width="1032" height="105" alt="image" src="https://github.com/user-attachments/assets/52de2531-5e3e-4707-8142-3970ed1d9569" />


```bash
sudo insmod monitor.ko
ls -l /dev/container_monitor
```

---

###  3. Supervisor Running

<img width="1062" height="67" alt="image" src="https://github.com/user-attachments/assets/78668736-dac3-49c9-89e2-05930d6f141d" />


```bash
sudo ./engine supervisor ./rootfs-base
```

---

###  4. Starting Containers

<img width="1057" height="137" alt="image" src="https://github.com/user-attachments/assets/22087bb6-c880-4c8d-ae8a-72239c1eea71" />


```bash
sudo ./engine start alpha ...
sudo ./engine start beta ...
```

---

###  5. Container Status
<img width="603" height="97" alt="image" src="https://github.com/user-attachments/assets/8285b7e5-856e-4385-9b9c-60c6adb64800" />


```bash
sudo ./engine ps
```

---

###  6. CPU Logs

<img width="689" height="233" alt="image" src="https://github.com/user-attachments/assets/7bc01cb3-5641-48d0-b1e6-17ae823cd817" />


```bash
sudo ./engine logs alpha
```

---

###  7. IO Logs

<img width="659" height="418" alt="image" src="https://github.com/user-attachments/assets/f656f626-fafc-4943-a00c-1345d21179ac" />


```bash
sudo ./engine logs beta
```

---

###  8. Memory Container Start

<img width="1057" height="88" alt="image" src="https://github.com/user-attachments/assets/e1f382a2-d5e5-4ccf-8aa6-01ffbd80dafd" />


```bash
sudo ./engine start mem1 ...
```

---

###  9. Memory Limit Logs

<img width="1068" height="418" alt="image" src="https://github.com/user-attachments/assets/589e9378-7c10-4e23-ad88-e0842e6a0cca" />
<img width="1280" height="800" alt="image" src="https://github.com/user-attachments/assets/e74a70e6-cd2a-4317-a2d7-b9c81d584a76" />


```bash
sudo dmesg | tail -n 30
```

---

###  10. Final Container Status

<img width="611" height="118" alt="image" src="https://github.com/user-attachments/assets/26a99d91-8115-41e1-8770-6e03ab3b341e" />


```bash
sudo ./engine ps
```

---

### 🔍 11. Process Check

<img width="1726" height="95" alt="image" src="https://github.com/user-attachments/assets/426c8a1a-507a-4786-a203-b2c2d3c3cc9f" />


```bash
ps aux | grep defunct
```

---

###  12. Logs Directory

<img width="1157" height="200" alt="image" src="https://github.com/user-attachments/assets/b826f4eb-c926-440a-b864-d13bc4467d8f" />


```bash
ls -l logs
```

---

##  Cleanup
<img width="687" height="125" alt="image" src="https://github.com/user-attachments/assets/86334535-5f4a-4b9a-93b3-7c0d96bbd495" />
<img width="718" height="61" alt="image" src="https://github.com/user-attachments/assets/f06e13b3-8bfe-40b6-a39f-ed9db638ae4f" />



```bash
sudo ./engine stop alpha
sudo ./engine stop beta
sudo ./engine stop mem1

sudo rmmod monitor
```



---

## ✅ Conclusion

This project demonstrates:

* Container runtime implementation in user space
* Kernel module integration
* Memory monitoring and enforcement
* Process isolation and logging

---
