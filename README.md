# Virtual Flow Layouts Plugin

**Virtual Flow Layouts** is a virtualized scrollable list widget for Unreal Engine 5. It is designed to handle thousands of complex, hierarchical UI elements with smooth scrolling and flexible layout strategies.

![VirtualFlowLayout](Docs/main.gif)

---

## Key Features

*   **UI Virtualization:** Only realizes widgets currently visible within the viewport and reuses them in a pool.
*   **Hierarchical Support:** Trees with variable expansion states.
*   **Heterogeneous Widgets:** Supports widgets of different classes and different layout requirements within the same list.
*   **Extensible Layout Engines:** Swap layout strategies (List, Tile, Masonry, Block Grid) by swapping `UVirtualFlowLayoutEngine` instances.
*   **Designer Preview:** Built-in preview generator allowing you to visualize and iterate on layouts directly within the UMG Designer.
*   **Navigation:** Native support for gamepad/keyboard spatial navigation and focus management.
*   **Debugger:** Built-in design-time debug drawing along with [InputFlowDebugger](https://github.com/MikeShatterwell/InputFlowDebugger) integration (if enabled in project) for more runtime debug options.

---

## Architecture Overview

### 1. `UVirtualFlowView`
The primary UMG component. It maintains the item data, the widget pool, and the connection between UObject data and Slate visuals. It exposes the public Blueprints API (e.g., `SetListItems`, `ScrollItemIntoView`, `ExpandAll`).

### 2. `SVirtualFlowView`
The low-level Slate implementation. It manages the layout pipeline:
1. **Flattening:** Converts the item tree into a flat display model.
2. **Layout Rebuild:** Uses a layout engine to calculate spatial positions.
3. **Realization:** Synchronizes widgets from the pool to the viewport.
4. **Measurement:** Performs Slate prepass measurements with configurable budgets to prevent scroll hitches.

### 3. Layout Engines (`UVirtualFlowLayoutEngine`)
Abstract strategy pattern for item arrangement.
*   **Flow Layout:** Default engine with flexible horizontal/vertical flow and wrapping.
*   **List:** Traditional linear stacking.
*   **Tile:** Grid-based wrapping.
*   **Masonry:** Pinterest-style variable-height packing.
*   **Block Grid:** Dense 2D grid for variable item row/column spans.
*   **Custom:** Implement your own by subclassing `UVirtualFlowLayoutEngine` and overriding `BuildLayout`.

### 4. Entry Widget Management
*   **`UVirtualFlowEntryWidgetExtension`**: Attached to realized widgets to track item data and enable communication back to the view (e.g., for notifying clicks/hovers)
*   **`IVirtualFlowEntryWidgetInterface`**: Interface for entry widgets to react to lifecycle events (`OnVirtualFlowItemObjectSet`, `OnVirtualFlowItemSelectionChanged`, etc.).
*   **`IVirtualFlowItem`**: Interface for data items to provide presentation and hierarchy data.

---

## Setup & Integration

### 1. Interface Implementation
Any `UObject` data you wish to display must implement the `IVirtualFlowItem` interface.
*   `GetVirtualFlowLayout()`: Returns metadata about how the item should be presented: widget class, layout preferences.
*   `GetVirtualFlowChildren()`: Returns the list of child items if the structure is hierarchical.

### 2. Adding to UMG
1. Drag the **Virtual Flow View** from the palette onto your User Widget.
2. Assign an **Entry Widget Class** (which should implement `IVirtualFlowEntryWidgetInterface` to receive data items).
3. Set the **Layout Engine** (defaults to Flow Layout).
4. Populate data using `SetListItems` or `AddListItem` in Blueprints.

---

## Configuration

*   **Virtualization Settings:** Adjust `OverscanPx` and `MaxMeasurementsPerTick` to balance visual smoothness against CPU overhead during rapid scrolling.

---

## Runtime Debugging

If the `InputFlowDebugger` is enabled in your project:
1. Open the plugin's overlay using `InputFlow.Overlay 1`
2. Click the `Extensions` dropdown and select `Virtual Flow Layout` to view the debug panel.

---

## Licensing
MIT License. Pull requests and contributions are welcome!