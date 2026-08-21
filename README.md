# Emper Backends

Backend implementations for the Emper simulation ecosystem.

Emper Backends provides concrete implementations of engine-level interfaces for platform-specific functionality such as rendering and compute.

The repository exists to keep platform and API-specific code separate from the simulation core.

---

## What Are Backends?

Emper Engine defines interfaces for functionality that may have multiple implementations.

Backends provide the actual implementations of those interfaces.

```text id="4s7j2n"
Emper Engine
    │
    │ defines interfaces
    ▼
Backend
    │
    └── platform / API specific implementation
```

This allows simulation code to remain independent from a particular graphics or compute API.

---

## Why Separate Backends?

Rendering and compute technologies are platform- and API-specific.

Embedding those implementations directly into the engine would increase coupling and make the core harder to evolve.

Instead:

```text id="g5zq2a"
Simulation
     │
     ▼
Emper Engine
     │
     ▼
Interface
     │
     ▼
Backend Implementation
     │
     ▼
Platform / API
```

The engine provides the contract.

The backend provides the implementation.

---

## Responsibilities

A backend may provide implementations for functionality such as:

* Rendering
* Compute
* Shader compilation
* GPU resources
* Platform integration
* Windowing and presentation

The exact capabilities depend on the backend and the interface it implements.

Backends should avoid containing simulation algorithms or domain-specific logic.

---

## Backend Independence

Backend implementations should remain isolated from each other whenever practical.

A simulation should not need to know whether a backend uses one graphics API or another.

For example:

```text id="v1w0fc"
Simulation
     │
     ▼
IRenderer
     │
     ├── Backend A
     │
     ├── Backend B
     │
     └── Backend C
```

The simulation depends on the interface rather than the implementation.

This makes it possible to change or add backends without redesigning the simulation itself.

---

## Compute Backends

Compute backends provide access to hardware-accelerated computation when an appropriate implementation is available.

They are intended to expose general compute capabilities to higher-level modules without requiring those modules to directly manage a specific compute API.

A module may therefore choose between different execution strategies when appropriate:

```text id="m4k8d1"
Simulation Module
       │
       ├── CPU implementation
       │
       └── GPU implementation
               │
               ▼
          Compute Backend
```

The decision of how an algorithm should be executed belongs to the module or application, not to the backend itself.

---

## Rendering Backends

Rendering backends provide concrete implementations for visualization.

Rendering is treated as a separate concern from simulation state and simulation algorithms.

This allows the same simulation to potentially be visualized through different rendering implementations or run without rendering at all.

```text id="n8y2p4"
Simulation
    │
    ├── Headless execution
    │
    └── Visualization
            │
            ▼
      Rendering Backend
```

---

## Backend Design Principles

### Follow Engine Interfaces

Backends should implement existing engine contracts rather than introducing simulation-specific interfaces into the core.

### Keep Platform Code Isolated

Platform- and API-specific code should remain inside the backend.

### No Domain Algorithms

Backends should provide infrastructure, not simulation algorithms.

### Minimize Coupling

A backend should expose only the functionality required by the interface it implements.

### Replaceability

Where practical, applications and modules should be able to switch backend implementations without changing simulation logic.

---

## Relationship to the Engine

The three layers have distinct responsibilities:

```text id="x9w1ra"
┌─────────────────────┐
│     Application     │
└──────────┬──────────┘
           │
┌──────────▼──────────┐
│      Modules        │
└──────────┬──────────┘
           │
┌──────────▼──────────┐
│   Emper Engine      │
│                     │
│   Interfaces        │
└──────────┬──────────┘
           │
┌──────────▼──────────┐
│      Backends       │
│                     │
│ Platform-specific   │
│ implementations     │
└─────────────────────┘
```

The engine defines what a backend must provide.

The backend determines how that functionality is implemented.

---

## Development

Backend implementations should be developed against the engine interfaces they implement.

When a backend exposes functionality that is not supported by an existing engine interface, the preferred approach is to first determine whether that capability represents a genuine engine-level abstraction.

New interfaces should emerge from real requirements rather than from a single backend's implementation details.

---

## Project Status

Emper Backends is under active development.

Backend implementations may change as the engine interfaces and real simulation workloads evolve.

The repository is intentionally organized around capabilities and interfaces rather than a fixed list of technologies.

---

## License

Apache License 2.0
