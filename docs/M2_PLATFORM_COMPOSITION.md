# M2 Platform composition baseline

Status: Draft. This baseline applies to issue #16. It does not freeze the source
API or ABI.

## Ownership

One `Platform` object owns board support and these services:

1. `InputService`
2. `PowerService`
3. `TimeService`
4. `StorageService`
5. `SystemService`
6. `DisplayService`

The application owns only the `Platform` object. It gets non-owning references
from `Platform`. It does not create, attach, or delete a service.

## Initialization

`Platform::Initialize()` performs these operations:

1. Initialize board support.
2. Attach InputService.
3. Attach PowerService.
4. Attach TimeService.
5. Create StorageService.
6. Attach SystemService.
7. Create DisplayService.
8. Create Diagnostics with typed references to the services.

The order preserves the qualified demo startup behavior. Display initialization
remains the last service operation before the splash screen.

If an operation fails, `Platform` destroys each service that it already created.
It destroys the services in reverse order. It does not retry board initialization
on the same object.

Allocation failure returns `ESP_ERR_NO_MEM`. The application calls service
accessors only after `Initialize()` returns `ESP_OK`. An accessor asserts this
precondition before it returns a reference.
If allocation fails before board initialization starts, the application can
retry initialization on the same Platform object.

## Application boundary

Application code can use `Display()`, `Input()`, `Power()`, `Time()`, `Storage()`,
and `System()`.

The application gets Diagnostics through `Diagnostics()`. The Platform API does
not expose board support. Diagnostics gets typed Input, Power, Time, Storage,
and System service references during composition. Diagnostic-only audio, LED,
and NFC operations remain private to the self-test implementation.
