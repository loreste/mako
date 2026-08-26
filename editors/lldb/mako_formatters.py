# Makori lldb data formatters.
#
# Loaded automatically by `mako dap` / `mako debug`, or manually:
#   (lldb) command script import /path/to/mako_formatters.py
#
# Provides:
#   - summary for MakoriString      -> "hello"
#   - synthetic children for Makori*Array / MakoArr_* ({T *data; size_t len; cap})
#     so slices display as indexed elements instead of a raw pointer.

import lldb


def mako_string_summary(valobj, internal_dict, options):
    data = valobj.GetChildMemberWithName("data")
    length = valobj.GetChildMemberWithName("len")
    if not data.IsValid() or not length.IsValid():
        return "<invalid MakoString>"
    n = length.GetValueAsUnsigned(0)
    if n == 0:
        return '""'
    ptr = data.GetValueAsUnsigned(0)
    if ptr == 0:
        return "<null>"
    error = lldb.SBError()
    raw = valobj.GetProcess().ReadMemory(ptr, n, error)
    if error.Fail() or raw is None:
        return "<unreadable>"
    text = raw.decode("utf-8", errors="replace")
    return '"%s"' % text.replace("\\", "\\\\").replace('"', '\\"')


def mako_array_summary(valobj, internal_dict, options):
    """Summary for Makori slice structs: [e0, e1, ...] (first 16 elements)."""
    # Bypass our own synthetic children to reach the real data/len members.
    raw = valobj.GetNonSyntheticValue()
    data = raw.GetChildMemberWithName("data")
    length = raw.GetChildMemberWithName("len")
    if not data.IsValid() or not length.IsValid():
        return "<invalid>"
    n = length.GetValueAsUnsigned(0)
    if n == 0:
        return "[]"
    elem_type = data.GetType().GetPointeeType()
    base = data.GetValueAsUnsigned(0)
    if base == 0:
        return "<null>"
    size = elem_type.GetByteSize()
    parts = []
    for i in range(min(n, 16)):
        elem = data.CreateValueFromAddress("[%d]" % i, base + i * size, elem_type)
        s = elem.GetSummary() or elem.GetValue()
        if s is None:
            s = "?"
        parts.append(s)
    if n > 16:
        parts.append("...")
    return "[%s]" % ", ".join(parts)


class MakoArrayProvider:
    """Synthetic children for Makori slice structs: {T *data; size_t len; size_t cap}."""

    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self.data = None
        self.len = 0
        self.elem_type = None

    def update(self):
        self.data = self.valobj.GetChildMemberWithName("data")
        length = self.valobj.GetChildMemberWithName("len")
        self.len = length.GetValueAsUnsigned(0) if length.IsValid() else 0
        self.elem_type = None
        if self.data.IsValid():
            t = self.data.GetType()
            if t.IsPointerType():
                self.elem_type = t.GetPointeeType()
        return False

    def num_children(self):
        return self.len

    def has_children(self):
        return self.len > 0

    def get_child_index(self, name):
        try:
            return int(name.lstrip("[").rstrip("]"))
        except ValueError:
            return -1

    def get_child_at_index(self, index):
        if index < 0 or index >= self.len or self.elem_type is None:
            return None
        if not self.data.IsValid():
            return None
        base = self.data.GetValueAsUnsigned(0)
        if base == 0:
            return None
        offset = index * self.elem_type.GetByteSize()
        return self.data.CreateValueFromAddress(
            "[%d]" % index, base + offset, self.elem_type
        )


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand(
        "type summary add -F mako_formatters.mako_string_summary "
        "-x '^MakoString$' --category Mako"
    )
    debugger.HandleCommand(
        "type synthetic add -l mako_formatters.MakoArrayProvider "
        "-x '^Mako(Int|Byte|Str|Float|Bool)Array$' --category Mako"
    )
    debugger.HandleCommand(
        "type synthetic add -l mako_formatters.MakoArrayProvider "
        "-x '^MakoArr_' --category Mako"
    )
    debugger.HandleCommand(
        "type summary add -F mako_formatters.mako_array_summary "
        "-x '^Mako(Int|Byte|Str|Float|Bool)Array$' --category Mako"
    )
    debugger.HandleCommand(
        "type summary add -F mako_formatters.mako_array_summary "
        "-x '^MakoArr_' --category Mako"
    )
    debugger.HandleCommand("type category enable Mako")
