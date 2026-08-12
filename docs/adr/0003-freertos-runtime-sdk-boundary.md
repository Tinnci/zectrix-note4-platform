# ADR-0003: Keep FreeRTOS below the Zectrix application runtime

Status: Accepted on 2026-08-12 for SDK v1.

## Context

ESP-IDF supplies IDF FreeRTOS, device drivers, timers, storage, networking, and
system services. The firmware also needs product concepts such as an
application, navigation, a lifecycle callback, a render request, and a power
transition.

Two reference patterns clarify the choice. A cooperative product loop can use
FreeRTOS without making each activity a task. A thicker embedded runtime can
use tasks, queues, and service threads internally. Neither pattern requires a
new kernel. The useful decision is where product semantics stop and execution
mechanisms start.

## Decision

IDF FreeRTOS remains the kernel and scheduler. Zectrix does not implement a
kernel or scheduler.

The Zectrix application runtime is a product runtime above FreeRTOS. It defines:

- one foreground application lifecycle;
- owned deferred navigation commands;
- render intent and foreground-generation invalidation;
- application IDs and a static registry;
- error and shutdown semantics.

An application, controller, activity, service, navigation request, or render
request is not an RTOS task. A service can use a task, queue, notification,
mutex, timer, or no separate RTOS object. The choice stays below the SDK and
must follow measured concurrency needs.

SDK v1 is a C++17 source-stable contract for statically linked applications.
The firmware and its applications build together with one toolchain. SDK v1
does not promise a C++ binary ABI. It does not support independently built or
dynamically loaded applications.

## Dependency direction

```text
Applications
    |
Zectrix SDK v1: lifecycle, input, commands, render intent, status
    |
Zectrix application runtime and firmware composition root
    |
Platform services and board support
    |
ESP-IDF services, IDF FreeRTOS, HAL and drivers
    |
ESP32-S3 hardware
```

The public SDK must not include or expose:

- ESP-IDF headers or `esp_err_t`;
- FreeRTOS headers, ticks, tasks, queues, semaphores, or event handles;
- board classes, raw driver handles, or the Platform implementation;
- an opaque factory context that requires an unchecked cast.

## Compatibility decision

Source compatibility is the justified promise for v1. The SDK already has
real static consumers, but it has no binary loader, executable format, symbol
resolver, process isolation, or independent toolchain contract. A C++ ABI
promise would constrain class layout, compiler versions, standard-library
types, exceptions, RTTI, allocation, and link rules without serving a current
product requirement.

A versioned C ABI is deferred. Reconsider it only when an independently built
binary or dynamic loader has a measured requirement. That later decision must
also define executable format, symbol versioning, memory safety, permissions,
failure containment, and upgrade behavior.

## Consequences

- SDK consumers rebuild with the firmware toolchain.
- A breaking source change requires SDK major version 2.
- Additive source-compatible changes can increment the minor version.
- Fixes that do not change the source contract can increment the patch version.
- Task topology can change without an application source change.
- Dynamic loading, application permissions, OTA, and partition layout remain
  separate architecture work.
