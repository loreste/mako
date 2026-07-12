//! Emit C from a typed Mako AST.

use crate::ast::*;
use std::collections::HashMap;
use std::fmt::Write as _;

#[derive(Clone)]
struct EnumInfo {
    c_name: String,
    /// variant name → (tag index, field types as "int"|"string"|"bool"|"float")
    variants: HashMap<String, (usize, Vec<&'static str>)>,
}

#[derive(Clone)]
struct StructInfo {
    c_name: String,
    /// field name → C type
    fields: Vec<(String, String)>,
}

pub struct Codegen {
    out: String,
    tmp: usize,
    indent: usize,
    /// Function name → C return type
    fn_rets: HashMap<String, String>,
    /// Function name → C param types
    fn_params: HashMap<String, Vec<String>>,
    /// Local variable → C type
    locals: HashMap<String, String>,
    /// Local `MakoChanPtr*` → Mako struct type name (for send/recv boxing)
    chan_ptr_elems: HashMap<String, String>,
    /// Enum type name → info
    enums: HashMap<String, EnumInfo>,
    /// Struct type name → info
    structs: HashMap<String, StructInfo>,
    /// Variant name → enum type name
    variant_to_enum: HashMap<String, String>,
    /// Extern "C" function names (unmangled)
    extern_fns: std::collections::HashSet<String>,
    /// Comptime const ints
    const_ints: HashMap<String, i64>,
    /// When set, emit Go-like test runner main instead of mako_main()
    test_fns: Option<Vec<String>>,
    /// When inside `arena name { }`, C name of the arena local (for arena-backed make).
    current_arena: Option<String>,
    /// Interface name → method names (for `recv.method(...)` → `Iface_method(...)`).
    interfaces: Vec<(String, Vec<String>)>,
    /// Pending `defer` bodies for the current function (LIFO on exit/return).
    defer_stack: Vec<Block>,
    /// NLL seed: `share let` names per block scope — auto `share_drop` on `}`.
    share_scopes: Vec<Vec<String>>,
    /// Shares still live (not yet dropped); avoids double-drop after explicit `share_drop`.
    share_live: std::collections::HashSet<String>,
    /// Nesting of `unsafe { }` — skip debug bounds checks when > 0.
    unsafe_depth: usize,
    /// Generic templates (not emitted; specialized as `name__tag`).
    generic_templates: HashMap<String, FnDef>,
    /// Keep bounds checks under NDEBUG when true.
    pub bounds_checks_always: bool,
    /// Integer overflow: Wrap (C default), Trap (abort), Ignore (same as wrap).
    pub overflow_mode: OverflowMode,
    /// Source path for `#line` directives (debug mapping).
    pub source_file: Option<String>,
    /// Emitted tuple typedef tags.
    tuple_typedefs: std::collections::HashSet<String>,
    /// Tuple typedef C snippets to hoist before forward decls.
    pending_tuple_typedefs: Vec<String>,
    /// When emitting a function whose return is `Result[T, Enum]`, the C enum type
    /// name for Err payload (e.g. `MakoEnum_IoError`). Used by match `Err(e)`.
    current_result_err_enum: Option<String>,
    /// Local/temp name → Result Err enum C type (for match on locals).
    result_err_enums: HashMap<String, String>,
    /// Function name → Result Err enum C type.
    fn_result_err_enum: HashMap<String, String>,
    /// Function name → Ok payload kind: "int" | "string" | "float" | "struct".
    fn_result_ok_kind: HashMap<String, String>,
    /// Function name → struct C name when Ok is a named struct.
    fn_result_ok_struct: HashMap<String, String>,
    /// Local Result binding → Ok payload kind.
    result_ok_kinds: HashMap<String, String>,
    /// Local Result → struct name for Ok struct payloads.
    result_ok_structs: HashMap<String, String>,
    /// Job local / temp name → C return type of the kicked function.
    job_rets: HashMap<String, String>,
    /// Job local / temp → Result Ok kind when ret is MakoResultInt.
    job_ok_kinds: HashMap<String, String>,
    /// Channel local / temp name that carries float via int-ring bitcast.
    chan_float: std::collections::HashSet<String>,
    /// Names of `const fn`s (bodies foldable at compile time when args are const).
    const_fns: HashMap<String, FnDef>,
}

pub use crate::overflow::OverflowMode;

impl Codegen {
    pub fn new() -> Self {
        Self {
            out: String::new(),
            tmp: 0,
            indent: 0,
            fn_rets: HashMap::new(),
            fn_params: HashMap::new(),
            locals: HashMap::new(),
            chan_ptr_elems: HashMap::new(),
            enums: HashMap::new(),
            structs: HashMap::new(),
            variant_to_enum: HashMap::new(),
            extern_fns: std::collections::HashSet::new(),
            const_ints: HashMap::new(),
            test_fns: None,
            current_arena: None,
            interfaces: Vec::new(),
            defer_stack: Vec::new(),
            share_scopes: Vec::new(),
            share_live: std::collections::HashSet::new(),
            unsafe_depth: 0,
            generic_templates: HashMap::new(),
            bounds_checks_always: false,
            overflow_mode: OverflowMode::Wrap,
            source_file: None,
            tuple_typedefs: std::collections::HashSet::new(),
            pending_tuple_typedefs: Vec::new(),
            current_result_err_enum: None,
            result_err_enums: HashMap::new(),
            fn_result_err_enum: HashMap::new(),
            fn_result_ok_kind: HashMap::new(),
            fn_result_ok_struct: HashMap::new(),
            result_ok_kinds: HashMap::new(),
            result_ok_structs: HashMap::new(),
            job_rets: HashMap::new(),
            job_ok_kinds: HashMap::new(),
            chan_float: std::collections::HashSet::new(),
            const_fns: HashMap::new(),
        }
    }

    /// Extract `MakoEnum_X` if `ty` is `Result[_, X]` and X is a known enum.
    fn result_err_enum_c(&self, ty: &TypeExpr) -> Option<String> {
        crate::errors::result_err_enum_c(ty, |en| self.enums.contains_key(en))
    }

    /// Ok payload kind for Result[T, _]: "string", "float", "struct", "slice",
    /// "map"/"map_si", "map_ii", "map_ss", or "int".
    fn result_ok_kind(ty: &TypeExpr) -> &'static str {
        match ty {
            TypeExpr::Generic(n, args) if n == "Result" && !args.is_empty() => {
                match &args[0] {
                    TypeExpr::Named(t) if t == "string" => "string",
                    TypeExpr::Named(t) if t == "float" || t == "float64" => "float",
                    TypeExpr::Array(_) => "slice",
                    TypeExpr::Map(k, v) => match (k.as_ref(), v.as_ref()) {
                        (TypeExpr::Named(kk), TypeExpr::Named(vv))
                            if kk == "int" && (vv == "int" || vv == "int64") =>
                        {
                            "map_ii"
                        }
                        (TypeExpr::Named(kk), TypeExpr::Named(vv))
                            if kk == "string" && vv == "string" =>
                        {
                            "map_ss"
                        }
                        // map[string]int (and unknown → SI)
                        _ => "map_si",
                    },
                    TypeExpr::Named(t)
                        if t != "int"
                            && t != "int64"
                            && t != "int32"
                            && t != "int8"
                            && t != "byte"
                            && t != "bool"
                            && t != "string"
                            && t != "float"
                            && t != "float64" =>
                    {
                        "struct"
                    }
                    _ => "int",
                }
            }
            _ => "int",
        }
    }

    /// True when every field is a reflect leaf (int/float/bool/string) or nested POD struct.
    fn fields_are_reflect_pod(&self, fields: &[(String, String)]) -> bool {
        fields.iter().all(|(_, ft)| {
            ft == "int64_t"
                || ft == "double"
                || ft == "bool"
                || ft == "MakoString"
                || self
                    .structs
                    .get(ft)
                    .or_else(|| self.structs.values().find(|s| s.c_name == *ft))
                    .map(|info| !info.fields.is_empty() && self.fields_are_reflect_pod(&info.fields))
                    .unwrap_or(false)
        })
    }

    /// Flatten POD (and nested POD) fields into `tmp` bag starting at `*idx`.
    fn emit_reflect_pod_fields(
        &mut self,
        tmp: &str,
        base: &str,
        fields: &[(String, String)],
        idx: &mut usize,
    ) {
        for (fname, ft) in fields {
            let access = format!("{base}.{fname}");
            if ft == "MakoString" {
                let s = self.fresh("rfs");
                self.line(&format!("MakoString {s} = mako_str_clone({access});"));
                self.line(&format!(
                    "mako_reflect_value_set_at({tmp}, {idx}, {s}); free({s}.data);"
                ));
                *idx += 1;
            } else if ft == "int64_t" || ft == "double" || ft == "bool" {
                let s = self.fresh("rfs");
                self.line(&format!(
                    "MakoString {s} = mako_int_to_string((int64_t)({access}));"
                ));
                self.line(&format!(
                    "mako_reflect_value_set_at({tmp}, {idx}, {s}); free({s}.data);"
                ));
                *idx += 1;
            } else if let Some(info) = self
                .structs
                .get(ft)
                .or_else(|| self.structs.values().find(|s| s.c_name == *ft))
                .cloned()
            {
                let nested = info.fields.clone();
                self.emit_reflect_pod_fields(tmp, &access, &nested, idx);
            }
        }
    }

    fn result_ok_struct_name(&self, ty: &TypeExpr) -> Option<String> {
        match ty {
            TypeExpr::Generic(n, args) if n == "Result" && !args.is_empty() => {
                if let TypeExpr::Named(t) = &args[0] {
                    if self.structs.contains_key(t) {
                        return Some(t.clone());
                    }
                }
                None
            }
            _ => None,
        }
    }

    fn push_share_scope(&mut self) {
        self.share_scopes.push(Vec::new());
    }

    /// Emit a bounds check unless inside `unsafe { }`.
    /// Uses `MAKO_BOUNDS_CHECK` from mako_rt.h (honors `MAKO_BOUNDS_ALWAYS` / NDEBUG).
    fn emit_bounds_check(&mut self, cond_c: &str, msg: &str) {
        if self.unsafe_depth > 0 {
            return;
        }
        self.line(&format!("MAKO_BOUNDS_CHECK({cond_c}, \"{msg}\");"));
    }

    fn pop_share_scope(&mut self) {
        if let Some(names) = self.share_scopes.pop() {
            for name in names.into_iter().rev() {
                if self.share_live.remove(&name) {
                    self.line(&format!("mako_share_drop({name});"));
                }
            }
        }
    }

    fn register_share_local(&mut self, name: &str) {
        self.share_live.insert(name.to_string());
        if let Some(scope) = self.share_scopes.last_mut() {
            scope.push(name.to_string());
        }
    }

    fn note_share_dropped(&mut self, name: &str) {
        self.share_live.remove(name);
        // Keep name in share_scopes so pop is a no-op for already-dropped.
    }

    /// Emit a Go-like test harness that runs each `TestXxx` via `mako_test_run`.
    pub fn with_tests(mut self, tests: Vec<String>) -> Self {
        self.test_fns = Some(tests);
        self
    }

    fn fresh(&mut self, prefix: &str) -> String {
        let id = self.tmp;
        self.tmp += 1;
        format!("{prefix}_{id}")
    }

    fn emit_byte_array_lit(&mut self, elems: &[Expr]) -> (String, String) {
        let tmp = self.fresh("barr");
        let vals: Vec<String> = elems
            .iter()
            .map(|e| {
                let (_, v) = self.emit_expr(e);
                v
            })
            .collect();
        let lit = self.fresh("blit");
        let body = if vals.is_empty() {
            "0".to_string()
        } else {
            vals.join(", ")
        };
        self.line(&format!("int64_t {lit}[] = {{ {body} }};"));
        self.line(&format!(
            "MakoByteArray {tmp} = mako_byte_array_of({lit}, {});",
            elems.len()
        ));
        ("MakoByteArray".into(), tmp)
    }

    fn emit_str_array_lit(&mut self, elems: &[Expr]) -> (String, String) {
        let tmp = self.fresh("sarr");
        let vals: Vec<String> = elems
            .iter()
            .map(|e| {
                let (_, v) = self.emit_expr(e);
                v
            })
            .collect();
        let lit = self.fresh("slit");
        let body = if vals.is_empty() {
            "{NULL, 0}".to_string()
        } else {
            vals.join(", ")
        };
        self.line(&format!("MakoString {lit}[] = {{ {body} }};"));
        self.line(&format!(
            "MakoStrArray {tmp} = mako_str_array_of({lit}, {});",
            elems.len()
        ));
        ("MakoStrArray".into(), tmp)
    }

    fn emit_float_array_lit(&mut self, elems: &[Expr]) -> (String, String) {
        let tmp = self.fresh("farr");
        let vals: Vec<String> = elems
            .iter()
            .map(|e| {
                let (_, v) = self.emit_expr(e);
                v
            })
            .collect();
        let lit = self.fresh("flit");
        let body = if vals.is_empty() {
            "0.0".to_string()
        } else {
            vals.join(", ")
        };
        self.line(&format!("double {lit}[] = {{ {body} }};"));
        self.line(&format!(
            "MakoFloatArray {tmp} = mako_float_array_of({lit}, {});",
            elems.len()
        ));
        ("MakoFloatArray".into(), tmp)
    }

    fn emit_struct_array_lit(&mut self, sn: &str, elems: &[Expr]) -> (String, String) {
        let tmp = self.fresh("sarr");
        let lit = self.fresh("slit");
        let arr_ty = format!("MakoArr_{sn}");
        if elems.is_empty() {
            self.line(&format!("{arr_ty} {tmp} = mako_arr_{sn}_make(0, 0);"));
            return (arr_ty, tmp);
        }
        self.line(&format!("{sn} {lit}[{}] ;", elems.len()));
        for (i, e) in elems.iter().enumerate() {
            let (_, v) = self.emit_expr(e);
            self.line(&format!("{lit}[{i}] = {v};"));
        }
        self.line(&format!(
            "{arr_ty} {tmp} = mako_arr_{sn}_of({lit}, {});",
            elems.len()
        ));
        (arr_ty, tmp)
    }

    fn line(&mut self, s: &str) {
        for _ in 0..self.indent {
            self.out.push_str("    ");
        }
        self.out.push_str(s);
        self.out.push('\n');
    }

    pub fn emit(mut self, program: &Program) -> String {
        // Native builds pull the full runtime surface. WASI preview1 builds
        // (`-DMAKO_WASI`) keep only `mako_rt.h` — sockets/TLS/DB need host POSIX.
        self.out.push_str("#include \"mako_rt.h\"\n");
        // Overflow mode for checked integer ops (must precede mako_overflow.h use).
        self.out.push_str(&format!(
            "#define MAKO_OVERFLOW_MODE {}\n",
            self.overflow_mode.c_define_value()
        ));
        // Default release-safe bounds when requested via codegen flag.
        if self.bounds_checks_always {
            self.out.push_str("#define MAKO_BOUNDS_ALWAYS 1\n");
            self.out.push_str("#ifndef NDEBUG\n/* bounds also forced under NDEBUG via MAKO_BOUNDS_ALWAYS */\n#endif\n");
        }
        self.out.push_str("#include \"mako_overflow.h\"\n");
        self.out.push_str("#ifndef MAKO_WASI\n");
        self.out.push_str("#include \"mako_uuid.h\"\n");
        self.out.push_str("#include \"mako_net.h\"\n");
        self.out.push_str("#include \"mako_proxy.h\"\n");
        self.out.push_str("#include \"mako_http.h\"\n");
        self.out.push_str("#include \"mako_trace.h\"\n"); /* before std so log_* can emit trace= */
        self.out.push_str("#include \"mako_std.h\"\n");
        self.out.push_str("#include \"mako_leak.h\"\n");
        self.out.push_str("#include \"mako_shutdown.h\"\n");
        self.out.push_str("#include \"mako_tls.h\"\n");
        self.out.push_str("#include \"mako_nghttp2.h\"\n");
        self.out.push_str("#include \"mako_quiche.h\"\n");
        self.out.push_str("#include \"mako_ws.h\"\n");
        self.out.push_str("#include \"mako_db.h\"\n");
        self.out.push_str("#include \"mako_cmap.h\"\n");
        self.out.push_str("#include \"mako_dio.h\"\n");
        self.out.push_str("#include \"mako_evloop.h\"\n");
        self.out.push_str("#include \"mako_game.h\"\n");
        self.out.push_str("#include \"mako_cloud.h\"\n");
        self.out.push_str("#include \"mako_httpengine.h\"\n");
        self.out.push_str("#endif /* MAKO_WASI */\n\n");

        // Collect interfaces for method dispatch sugar
        for item in &program.items {
            if let Item::Interface(iface) = item {
                let methods: Vec<String> =
                    iface.methods.iter().map(|(n, _, _)| n.clone()).collect();
                self.interfaces.push((iface.name.clone(), methods));
            }
        }

        // Collect enums and emit C tagged unions
        for item in &program.items {
            if let Item::Enum(e) = item {
                self.register_enum(e);
            }
        }
        for item in &program.items {
            if let Item::Enum(e) = item {
                self.emit_enum_typedef(e);
            }
        }

        // Collect structs and emit C structs + []T helpers
        for item in &program.items {
            if let Item::Struct(s) = item {
                self.register_struct(s);
            }
        }
        for item in &program.items {
            if let Item::Struct(s) = item {
                self.emit_struct_typedef(s);
            }
        }
        // Register Mako struct schemas for reflect_type_schema / reflect_value_of_type.
        for item in &program.items {
            if let Item::Struct(s) = item {
                self.emit_struct_reflect_schema(s);
            }
        }

        // Collect fn metadata first so iface fat pointers can use signatures,
        // then emit iface typedefs before forward declarations that reference them.
        for item in &program.items {
            match item {
                Item::Fn(f) => {
                    if !f.type_params.is_empty() {
                        self.generic_templates.insert(f.name.clone(), f.clone());
                        continue; // templates are not emitted; mono specializations are
                    }
                    let ret = self.c_ret_type_resolved(f);
                    self.fn_rets.insert(f.name.clone(), ret);
                    if let Some(rt) = f.ret.as_ref() {
                        if let Some(ec) = self.result_err_enum_c(rt) {
                            self.fn_result_err_enum.insert(f.name.clone(), ec);
                        }
                        if matches!(rt, TypeExpr::Generic(n, _) if n == "Result") {
                            self.fn_result_ok_kind
                                .insert(f.name.clone(), Self::result_ok_kind(rt).into());
                            if let Some(sn) = self.result_ok_struct_name(rt) {
                                self.fn_result_ok_struct.insert(f.name.clone(), sn);
                            }
                        }
                    }
                    let param_tys: Vec<String> =
                        f.params.iter().map(|p| self.type_expr_c(&p.ty)).collect();
                    self.fn_params.insert(f.name.clone(), param_tys);
                    if let Some(ec) = f.ret.as_ref().and_then(|t| self.result_err_enum_c(t)) {
                        self.fn_result_err_enum.insert(f.name.clone(), ec);
                    }
                    if f.is_const {
                        self.const_fns.insert(f.name.clone(), f.clone());
                    }
                }
                Item::ExternC(ext) => {
                    let ret = ext
                        .ret
                        .as_ref()
                        .map(|t| self.type_expr_c(t))
                        .unwrap_or_else(|| "void".into());
                    self.fn_rets.insert(ext.name.clone(), ret);
                    self.fn_params.insert(
                        ext.name.clone(),
                        ext.params.iter().map(|p| self.type_expr_c(&p.ty)).collect(),
                    );
                    self.extern_fns.insert(ext.name.clone());
                }
                _ => {}
            }
        }

        // Fat-pointer interface typedefs (before forward decls that use them)
        self.emit_iface_typedefs();

        // Source map seed for debuggers (maps generated C back to .mko)
        if let Some(src) = &self.source_file {
            let _ = writeln!(self.out, "#line 1 \"{}\"", escape_c(src));
        }

        // Hoist all tuple typedefs needed by signatures (and known literal tuples).
        self.predeclare_tuples(program);

        // Forward declarations
        for item in &program.items {
            match item {
                Item::Fn(f) => {
                    if !f.type_params.is_empty() {
                        continue;
                    }
                    let ret = self
                        .fn_rets
                        .get(&f.name)
                        .cloned()
                        .unwrap_or_else(|| "void".into());
                    let params = self.c_params_resolved(f);
                    let _ = writeln!(self.out, "{ret} {name}({params});", name = mangle(&f.name));
                }
                Item::ExternC(ext) => {
                    let ret = self
                        .fn_rets
                        .get(&ext.name)
                        .cloned()
                        .unwrap_or_else(|| "void".into());
                    let params = if ext.params.is_empty() {
                        "void".into()
                    } else {
                        ext.params
                            .iter()
                            .map(|p| format!("{} {}", self.type_expr_c(&p.ty), p.name))
                            .collect::<Vec<_>>()
                            .join(", ")
                    };
                    let _ = writeln!(self.out, "extern {ret} {name}({params});", name = ext.name);
                }
                _ => {}
            }
        }

        // Vtable wrappers need forward decls of Iface_method impls
        self.emit_iface_vtables();

        self.out.push_str("\n/*__MAKO_HELPERS__*/\n\n");

        for item in &program.items {
            match item {
                Item::Fn(f) => {
                    if f.type_params.is_empty() {
                        self.emit_fn(f);
                    }
                }
                Item::Struct(_)
                | Item::Enum(_)
                | Item::Actor(_)
                | Item::Interface(_)
                | Item::On(_)
                | Item::Package { .. }
                | Item::Import { .. } => {}
                Item::ExternC(_) => {}
                Item::Const(c) => {
                    if let Some(v) = fold_const_c_env(&c.value, &self.const_ints, &self.const_fns) {
                        self.const_ints.insert(c.name.clone(), v);
                        // Emit as #define so uses in C are valid integer constants
                        let _ = writeln!(
                            self.out,
                            "#define {} ((int64_t){})",
                            mangle(&c.name),
                            v
                        );
                    }
                }
            }
        }

        if let Some(tests) = &self.test_fns {
            self.out.push_str("\nint main(int argc, char **argv) {\n");
            self.out.push_str("    mako_set_args(argc, argv);\n");
            self.out.push_str("    mako_test_install_crash_handler();\n");
            self.out.push_str("    int failed = 0;\n");
            for t in tests {
                let _ = writeln!(self.out, "    failed += mako_test_run(\"{t}\", {t});");
            }
            self.out.push_str(
                "    if (failed) {\n        fprintf(stderr, \"FAIL\\n\");\n        return 1;\n    }\n",
            );
            self.out
                .push_str("    printf(\"PASS\\n\");\n    fflush(stdout);\n    return 0;\n}\n");
        } else {
            self.out.push_str(
                "\nint main(int argc, char **argv) {\n    mako_set_args(argc, argv);\n    mako_main();\n    return 0;\n}\n",
            );
        }
        self.out
    }

    fn register_enum(&mut self, e: &EnumDef) {
        let c_name = format!("MakoEnum_{}", e.name);
        let mut variants = HashMap::new();
        for (i, v) in e.variants.iter().enumerate() {
            let fields: Vec<&'static str> = v
                .fields
                .iter()
                .map(|t| match t {
                    TypeExpr::Named(n) => match n.as_str() {
                        "string" => "string",
                        "bool" => "bool",
                        "float" => "float",
                        _ => "int",
                    },
                    _ => "int",
                })
                .collect();
            variants.insert(v.name.clone(), (i, fields));
            self.variant_to_enum.insert(v.name.clone(), e.name.clone());
        }
        self.enums
            .insert(e.name.clone(), EnumInfo { c_name, variants });
    }

    fn emit_enum_typedef(&mut self, e: &EnumDef) {
        let info = self.enums.get(&e.name).unwrap();
        let c_name = info.c_name.clone();
        let _ = writeln!(self.out, "typedef struct {{");
        let _ = writeln!(self.out, "    int tag;");
        let _ = writeln!(self.out, "    int64_t i0;");
        let _ = writeln!(self.out, "    int64_t i1;");
        let _ = writeln!(self.out, "    int64_t i2;");
        let _ = writeln!(self.out, "    MakoString s0;");
        let _ = writeln!(self.out, "    MakoString s1;");
        let _ = writeln!(self.out, "}} {c_name};");
        let _ = writeln!(self.out);
    }

    fn register_struct(&mut self, s: &StructDef) {
        let c_name = s.name.clone();
        let fields: Vec<(String, String)> = s
            .fields
            .iter()
            .map(|(n, t)| (n.clone(), self.type_expr_c(t)))
            .collect();
        self.structs
            .insert(s.name.clone(), StructInfo { c_name, fields });
    }

    fn emit_iface_typedefs(&mut self) {
        for (iname, methods) in self.interfaces.clone() {
            let iface_c = format!("MakoIface_{iname}");
            let vt_c = format!("MakoIface_{iname}_VTable");
            let _ = writeln!(self.out, "typedef struct {vt_c} {vt_c};");
            let _ = writeln!(self.out, "typedef struct {{");
            let _ = writeln!(self.out, "    void *data;");
            let _ = writeln!(self.out, "    const {vt_c} *vtable;");
            let _ = writeln!(self.out, "}} {iface_c};");
            let _ = writeln!(self.out, "struct {vt_c} {{");
            for m in &methods {
                // Prefer Iface_method; else any Iface_Concrete_method for signature.
                let mut key = format!("{iname}_{m}");
                if !self.fn_params.contains_key(&key) {
                    let prefix = format!("{iname}_");
                    let suffix = format!("_{m}");
                    for fname in self.fn_params.keys() {
                        if let Some(rest) = fname.strip_prefix(&prefix) {
                            if let Some(cn) = rest.strip_suffix(&suffix) {
                                if !cn.is_empty() && !cn.contains('_') {
                                    key = fname.clone();
                                    break;
                                }
                            }
                        }
                    }
                }
                let ret = self
                    .fn_rets
                    .get(&key)
                    .cloned()
                    .unwrap_or_else(|| "int64_t".into());
                let params = self.fn_params.get(&key).cloned().unwrap_or_default();
                let arg_tys: Vec<String> =
                    if !params.is_empty() && self.structs.values().any(|s| s.c_name == params[0]) {
                        params[1..].to_vec()
                    } else {
                        params.clone()
                    };
                let mut sig = vec!["void *self".to_string()];
                for (i, t) in arg_tys.iter().enumerate() {
                    sig.push(format!("{t} a{i}"));
                }
                let _ = writeln!(self.out, "    {ret} (*{m})({});", sig.join(", "));
            }
            let _ = writeln!(self.out, "}};");
            let _ = writeln!(self.out);
        }
    }

    fn emit_iface_vtables(&mut self) {
        for (iname, methods) in self.interfaces.clone() {
            let mut concretes: Vec<String> = Vec::new();
            let mut has_no_self = false;
            for m in &methods {
                let prefix = format!("{iname}_");
                let suffix = format!("_{m}");
                for fname in self.fn_params.keys() {
                    if let Some(rest) = fname.strip_prefix(&prefix) {
                        if let Some(cn) = rest.strip_suffix(&suffix) {
                            if !cn.is_empty()
                                && !cn.contains('_')
                                && self.structs.values().any(|s| s.c_name == *cn)
                            {
                                if !concretes.iter().any(|c| c == cn) {
                                    concretes.push(cn.to_string());
                                }
                            }
                        }
                    }
                }
                let key = format!("{iname}_{m}");
                if let Some(params) = self.fn_params.get(&key) {
                    if let Some(p0) = params.first() {
                        if self.structs.values().any(|s| &s.c_name == p0) {
                            if !concretes.iter().any(|c| c == p0) {
                                concretes.push(p0.clone());
                            }
                        } else {
                            has_no_self = true;
                        }
                    } else {
                        has_no_self = true;
                    }
                }
            }
            let iface_c = format!("MakoIface_{iname}");
            let vt_c = format!("MakoIface_{iname}_VTable");

            for concrete in &concretes {
                for m in &methods {
                    let keyed = format!("{iname}_{concrete}_{m}");
                    let key = if self.fn_params.contains_key(&keyed) {
                        keyed
                    } else {
                        format!("{iname}_{m}")
                    };
                    let ret = self
                        .fn_rets
                        .get(&key)
                        .cloned()
                        .unwrap_or_else(|| "int64_t".into());
                    let params = self.fn_params.get(&key).cloned().unwrap_or_default();
                    let has_self = !params.is_empty() && params[0] == *concrete;
                    let arg_tys: Vec<String> = if has_self {
                        params[1..].to_vec()
                    } else {
                        params.clone()
                    };
                    let wrap = format!("__mako_iface_{iname}_{m}_{concrete}");
                    let mut sig = vec!["void *self".to_string()];
                    let mut call_args = Vec::new();
                    if has_self {
                        call_args.push(format!("*({concrete} *)self"));
                    }
                    for (i, t) in arg_tys.iter().enumerate() {
                        sig.push(format!("{t} a{i}"));
                        call_args.push(format!("a{i}"));
                    }
                    let _ = writeln!(self.out, "static {ret} {wrap}({}) {{", sig.join(", "));
                    if ret == "void" {
                        let _ =
                            writeln!(self.out, "    {}({});", mangle(&key), call_args.join(", "));
                    } else {
                        let _ = writeln!(
                            self.out,
                            "    return {}({});",
                            mangle(&key),
                            call_args.join(", ")
                        );
                    }
                    let _ = writeln!(self.out, "}}");
                }
                let vt_inst = format!("MakoIface_{iname}_VTable_{concrete}");
                let _ = writeln!(self.out, "static const {vt_c} {vt_inst} = {{");
                for m in &methods {
                    let wrap = format!("__mako_iface_{iname}_{m}_{concrete}");
                    let _ = writeln!(self.out, "    .{m} = {wrap},");
                }
                let _ = writeln!(self.out, "}};");
                let box_fn = format!("mako_iface_{iname}_from_{concrete}");
                let _ = writeln!(
                    self.out,
                    "static inline {iface_c} {box_fn}({concrete} v) {{"
                );
                let _ = writeln!(
                    self.out,
                    "    {concrete} *p = ({concrete} *)malloc(sizeof({concrete}));"
                );
                let _ = writeln!(self.out, "    if (!p) mako_abort(\"iface box OOM\");");
                let _ = writeln!(self.out, "    *p = v;");
                let _ = writeln!(
                    self.out,
                    "    return ({iface_c}){{ .data = p, .vtable = &{vt_inst} }};"
                );
                let _ = writeln!(self.out, "}}");
            }

            if has_no_self || concretes.is_empty() {
                let mut ok_unit = !methods.is_empty();
                for m in &methods {
                    let key = format!("{iname}_{m}");
                    match self.fn_params.get(&key) {
                        Some(params)
                            if params
                                .first()
                                .map(|p| self.structs.values().any(|s| &s.c_name == p))
                                .unwrap_or(false) =>
                        {
                            ok_unit = false;
                        }
                        Some(_) => {}
                        None => ok_unit = false,
                    }
                }
                if ok_unit {
                    for m in &methods {
                        let key = format!("{iname}_{m}");
                        let ret = self
                            .fn_rets
                            .get(&key)
                            .cloned()
                            .unwrap_or_else(|| "int64_t".into());
                        let params = self.fn_params.get(&key).cloned().unwrap_or_default();
                        let wrap = format!("__mako_iface_{iname}_{m}_unit");
                        let mut sig = vec!["void *self".to_string()];
                        let mut call_args = Vec::new();
                        for (i, t) in params.iter().enumerate() {
                            sig.push(format!("{t} a{i}"));
                            call_args.push(format!("a{i}"));
                        }
                        let _ = writeln!(self.out, "static {ret} {wrap}({}) {{", sig.join(", "));
                        let _ = writeln!(self.out, "    (void)self;");
                        if ret == "void" {
                            let _ = writeln!(
                                self.out,
                                "    {}({});",
                                mangle(&key),
                                call_args.join(", ")
                            );
                        } else {
                            let _ = writeln!(
                                self.out,
                                "    return {}({});",
                                mangle(&key),
                                call_args.join(", ")
                            );
                        }
                        let _ = writeln!(self.out, "}}");
                    }
                    let vt_inst = format!("MakoIface_{iname}_VTable_unit");
                    let _ = writeln!(self.out, "static const {vt_c} {vt_inst} = {{");
                    for m in &methods {
                        let wrap = format!("__mako_iface_{iname}_{m}_unit");
                        let _ = writeln!(self.out, "    .{m} = {wrap},");
                    }
                    let _ = writeln!(self.out, "}};");
                    let box_fn = format!("mako_iface_{iname}_from_int64_t");
                    let _ = writeln!(self.out, "static inline {iface_c} {box_fn}(int64_t v) {{");
                    let _ = writeln!(self.out, "    (void)v;");
                    let _ = writeln!(
                        self.out,
                        "    return ({iface_c}){{ .data = NULL, .vtable = &{vt_inst} }};"
                    );
                    let _ = writeln!(self.out, "}}");
                }
            }
            let _ = writeln!(self.out);
        }
    }

    fn emit_struct_typedef(&mut self, s: &StructDef) {
        let info = self.structs.get(&s.name).unwrap().clone();
        let c_name = info.c_name.clone();
        let _ = writeln!(self.out, "typedef struct {{");
        for (fname, fty) in &info.fields {
            let _ = writeln!(self.out, "    {fty} {fname};");
        }
        let _ = writeln!(self.out, "}} {c_name};");
        let arr = format!("MakoArr_{c_name}");
        let _ = writeln!(self.out, "typedef struct {{");
        let _ = writeln!(self.out, "    {c_name} *data;");
        let _ = writeln!(self.out, "    size_t len;");
        let _ = writeln!(self.out, "    size_t cap;");
        let _ = writeln!(self.out, "}} {arr};");
        let _ = writeln!(
            self.out,
            "static inline {arr} mako_arr_{c_name}_make(int64_t len, int64_t cap) {{"
        );
        let _ = writeln!(self.out, "    if (len < 0) len = 0;");
        let _ = writeln!(self.out, "    if (cap < len) cap = len;");
        let _ = writeln!(self.out, "    {arr} a;");
        let _ = writeln!(
            self.out,
            "    a.data = ({c_name} *)calloc((size_t)(cap ? cap : 1), sizeof({c_name}));"
        );
        let _ = writeln!(self.out, "    a.len = (size_t)len;");
        let _ = writeln!(self.out, "    a.cap = (size_t)(cap ? cap : 1);");
        let _ = writeln!(self.out, "    return a;");
        let _ = writeln!(self.out, "}}");
        let _ = writeln!(
            self.out,
            "static inline int64_t mako_arr_{c_name}_len({arr} a) {{ return (int64_t)a.len; }}"
        );
        let _ = writeln!(
            self.out,
            "static inline int64_t mako_arr_{c_name}_cap({arr} a) {{ return (int64_t)a.cap; }}"
        );
        let _ = writeln!(
            self.out,
            "static inline {c_name} mako_arr_{c_name}_get({arr} a, int64_t i) {{"
        );
        let _ = writeln!(
            self.out,
            "#ifndef NDEBUG\n    if (i < 0 || (size_t)i >= a.len) mako_abort(\"struct slice index out of bounds\");\n#endif"
        );
        let _ = writeln!(self.out, "    return a.data[i];");
        let _ = writeln!(self.out, "}}");
        let _ = writeln!(
            self.out,
            "static inline void mako_arr_{c_name}_set({arr} a, int64_t i, {c_name} v) {{"
        );
        let _ = writeln!(
            self.out,
            "#ifndef NDEBUG\n    if (i < 0 || (size_t)i >= a.len) mako_abort(\"struct slice index out of bounds\");\n#endif"
        );
        let _ = writeln!(self.out, "    a.data[i] = v;");
        let _ = writeln!(self.out, "}}");
        let _ = writeln!(
            self.out,
            "static inline {arr} mako_arr_{c_name}_append({arr} s, {c_name} v) {{"
        );
        let _ = writeln!(self.out, "    if (s.len + 1 > s.cap) {{");
        let _ = writeln!(self.out, "        size_t ncap = s.cap ? s.cap * 2 : 1;");
        let _ = writeln!(self.out, "        if (ncap < s.len + 1) ncap = s.len + 1;");
        let _ = writeln!(
            self.out,
            "        {c_name} *nd = ({c_name} *)realloc(s.data, ncap * sizeof({c_name}));"
        );
        let _ = writeln!(
            self.out,
            "        if (!nd) mako_abort(\"append: out of memory\");"
        );
        let _ = writeln!(self.out, "        s.data = nd;");
        let _ = writeln!(self.out, "        s.cap = ncap;");
        let _ = writeln!(self.out, "    }}");
        let _ = writeln!(self.out, "    s.data[s.len++] = v;");
        let _ = writeln!(self.out, "    return s;");
        let _ = writeln!(self.out, "}}");
        let _ = writeln!(
            self.out,
            "static inline {arr} mako_arr_{c_name}_arena_append(MakoArena *arena, {arr} s, {c_name} v) {{"
        );
        let _ = writeln!(self.out, "    if (s.len + 1 > s.cap) {{");
        let _ = writeln!(self.out, "        size_t ncap = s.cap ? s.cap * 2 : 1;");
        let _ = writeln!(self.out, "        if (ncap < s.len + 1) ncap = s.len + 1;");
        let _ = writeln!(
            self.out,
            "        {c_name} *nd = ({c_name} *)mako_arena_alloc(arena, ncap * sizeof({c_name}));"
        );
        let _ = writeln!(
            self.out,
            "        if (s.len) memcpy(nd, s.data, s.len * sizeof({c_name}));"
        );
        let _ = writeln!(self.out, "        s.data = nd;");
        let _ = writeln!(self.out, "        s.cap = ncap;");
        let _ = writeln!(self.out, "    }}");
        let _ = writeln!(self.out, "    s.data[s.len++] = v;");
        let _ = writeln!(self.out, "    return s;");
        let _ = writeln!(self.out, "}}");
        let _ = writeln!(
            self.out,
            "static inline {arr} mako_arr_{c_name}_of(const {c_name} *vals, size_t n) {{"
        );
        let _ = writeln!(
            self.out,
            "    {arr} a = mako_arr_{c_name}_make((int64_t)n, (int64_t)n);"
        );
        let _ = writeln!(
            self.out,
            "    if (n) memcpy(a.data, vals, n * sizeof({c_name}));"
        );
        let _ = writeln!(self.out, "    return a;");
        let _ = writeln!(self.out, "}}");
        let _ = writeln!(
            self.out,
            "static inline {arr} mako_arr_{c_name}_arena_make(MakoArena *arena, int64_t len, int64_t cap) {{"
        );
        let _ = writeln!(self.out, "    if (len < 0) len = 0;");
        let _ = writeln!(self.out, "    if (cap < len) cap = len;");
        let _ = writeln!(self.out, "    {arr} a;");
        let _ = writeln!(
            self.out,
            "    a.data = ({c_name} *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof({c_name}));"
        );
        let _ = writeln!(
            self.out,
            "    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof({c_name}));"
        );
        let _ = writeln!(self.out, "    a.len = (size_t)len;");
        let _ = writeln!(self.out, "    a.cap = (size_t)(cap ? cap : 1);");
        let _ = writeln!(self.out, "    return a;");
        let _ = writeln!(self.out, "}}");
        let _ = writeln!(self.out);
    }

    /// Emit `mako_reflect_register_type("Name", "Field:type,...")` for live schema lookup.
    fn emit_struct_reflect_schema(&mut self, s: &StructDef) {
        let schema = s
            .fields
            .iter()
            .map(|(n, t)| format!("{n}:{}", type_expr_schema(t)))
            .collect::<Vec<_>>()
            .join(",");
        let name_esc = escape_c(&s.name);
        let schema_esc = escape_c(&schema);
        // clang/gcc constructor — runs before main (Mako's C backend).
        let _ = writeln!(
            self.out,
            "static void __attribute__((constructor)) __mako_reflect_reg_{}(void) {{",
            s.name
        );
        let _ = writeln!(
            self.out,
            "    (void)mako_reflect_register_type(\"{name_esc}\", \"{schema_esc}\");"
        );
        let _ = writeln!(self.out, "}}");
        let _ = writeln!(self.out);
    }

    fn c_ret_type_resolved(&self, f: &FnDef) -> String {
        match &f.ret {
            None => "void".into(),
            Some(t) => self.type_expr_c(t),
        }
    }

    fn c_params_resolved(&self, f: &FnDef) -> String {
        if f.params.is_empty() {
            return "void".into();
        }
        f.params
            .iter()
            .map(|p| format!("{} {}", self.type_expr_c(&p.ty), mangle(&p.name)))
            .collect::<Vec<_>>()
            .join(", ")
    }

    fn type_expr_c(&self, t: &TypeExpr) -> String {
        match t {
            TypeExpr::Named(n) => match n.as_str() {
                "int" | "int64" | "int32" | "int8" | "uint64" | "byte" => "int64_t".into(),
                "float" | "float64" => "double".into(),
                "bool" => "bool".into(),
                "string" => "MakoString".into(),
                "void" => "void".into(),
                "Arena" => "MakoArena".into(),
                "StrBuilder" => "MakoStrBuilder*".into(),
                "Mutex" => "MakoMutex*".into(),
                "RWMutex" => "MakoRWMutex*".into(),
                "CMap" => "MakoCMap*".into(),
                "MMap" => "MakoMMap*".into(),
                "EvLoop" => "MakoEvLoop*".into(),
                "Buf" => "MakoBuf*".into(),
                "GameUDP" => "MakoGameUDP*".into(),
                "Http2Conn" => "MakoHttp2Conn*".into(),
                "TlsServer" | "TlsConn" | "Watcher" => "void*".into(),
                "CHash" => "MakoCHash*".into(),
                "RateLimiter" => "MakoRateLimiter*".into(),
                "CircuitBreaker" => "MakoCircuitBreaker*".into(),
                "HttpEngine" => "MakoHttpEngine*".into(),
                "BufReader" => "MakoBufReader*".into(),
                "BufWriter" => "MakoBufWriter*".into(),
                "HttpRequest" => "MakoHttpRequest".into(),
                "HttpForwardResult" => "MakoHttpForwardResult".into(),
                "ProxyIoResult" => "MakoProxyIoResult".into(),
                "HttpParsed" => "MakoHttpParsed".into(),
                "SqlDB" => "MakoSqlDB".into(),
                "WaitGroup" => "MakoWaitGroup*".into(),
                "AtomicInt" => "MakoAtomicInt*".into(),
                "BytesBuffer" => "MakoBytesBuffer*".into(),
                "Uuid" => "MakoUuid".into(),
                "ShareInt" => "MakoShareInt".into(),
                "Slice" => "MakoSlice".into(),
                "PgConn" => "MakoPgConn".into(),
                "Secret" => "MakoSecret".into(),
                "MysqlConn" => "MakoMysqlConn".into(),
                "RedisConn" => "MakoRedisConn".into(),
                other if self.enums.contains_key(other) => self.enums[other].c_name.clone(),
                other if self.structs.contains_key(other) => self.structs[other].c_name.clone(),
                other if self.interfaces.iter().any(|(n, _)| n == other) => {
                    format!("MakoIface_{other}")
                }
                _ => "int64_t".into(),
            },
            TypeExpr::Array(inner) if matches!(inner.as_ref(), TypeExpr::Named(n) if n == "byte") => {
                "MakoByteArray".into()
            }
            TypeExpr::Array(inner) if matches!(inner.as_ref(), TypeExpr::Named(n) if n == "string") => {
                "MakoStrArray".into()
            }
            TypeExpr::Array(inner) if matches!(inner.as_ref(), TypeExpr::Named(n) if n == "float" || n == "float64") => {
                "MakoFloatArray".into()
            }
            TypeExpr::Array(inner) if matches!(inner.as_ref(), TypeExpr::Named(n) if self.structs.contains_key(n)) =>
            {
                let n = match inner.as_ref() {
                    TypeExpr::Named(n) => n.as_str(),
                    _ => "",
                };
                format!("MakoArr_{n}")
            }
            TypeExpr::Array(_) => "MakoIntArray".into(),
            TypeExpr::Map(k, v) => match (k.as_ref(), v.as_ref()) {
                (TypeExpr::Named(kk), TypeExpr::Named(vv)) if kk == "string" && vv == "int" => {
                    "MakoMapSI*".into()
                }
                (TypeExpr::Named(kk), TypeExpr::Named(vv)) if kk == "int" && vv == "int" => {
                    "MakoMapII*".into()
                }
                (TypeExpr::Named(kk), TypeExpr::Named(vv)) if kk == "string" && vv == "string" => {
                    "MakoMapSS*".into()
                }
                _ => "MakoMapSI*".into(),
            },
            TypeExpr::Generic(n, _) if n == "Result" => "MakoResultInt".into(),
            TypeExpr::Generic(n, _) if n == "Option" => "MakoOptionInt".into(),
            TypeExpr::Generic(n, args) if n == "chan" => {
                if matches!(args.first(), Some(TypeExpr::Named(t)) if t == "string") {
                    "MakoChanStr*".into()
                } else {
                    "MakoChan*".into()
                }
            }
            TypeExpr::Generic(n, _) if n == "List" => "MakoIntArray".into(),
            TypeExpr::Tuple(elems) => {
                let tag = elems
                    .iter()
                    .map(|e| c_type_mono_tag(&self.type_expr_c(e)))
                    .collect::<Vec<_>>()
                    .join("_");
                format!("MakoTup_{tag}")
            }
            TypeExpr::Fn(_, _) => "void*".into(),
            _ => "int64_t".into(),
        }
    }

    fn emit_fn(&mut self, f: &FnDef) {
        self.locals.clear();
        self.defer_stack.clear();
        self.share_scopes.clear();
        self.share_live.clear();
        self.result_err_enums.clear();
        self.current_result_err_enum = f
            .ret
            .as_ref()
            .and_then(|t| self.result_err_enum_c(t));
        self.push_share_scope();
        for p in &f.params {
            self.locals.insert(p.name.clone(), self.type_expr_c(&p.ty));
        }
        let ret = self.c_ret_type_resolved(f);
        let params = self.c_params_resolved(f);
        let _ = writeln!(
            self.out,
            "{ret} {name}({params}) {{",
            name = mangle(&f.name)
        );
        self.indent = 1;
        let ret_void =
            f.ret.is_none() || matches!(f.ret.as_ref(), Some(TypeExpr::Named(n)) if n == "void");
        let stmts = &f.body.stmts;
        let implicit_return = !ret_void
            && !stmts.iter().any(|s| matches!(s, Stmt::Return(_)))
            && matches!(stmts.last(), Some(Stmt::Expr(_)));

        let emit_count = if implicit_return {
            stmts.len().saturating_sub(1)
        } else {
            stmts.len()
        };
        for stmt in &stmts[..emit_count] {
            self.emit_stmt(stmt);
        }
        if implicit_return {
            if let Some(Stmt::Expr(e)) = stmts.last() {
                let (_, val) = self.emit_expr(e);
                self.pop_share_scope();
                self.emit_defers();
                self.line(&format!("return {val};"));
            }
        } else if !ret_void && !stmts.iter().any(|s| matches!(s, Stmt::Return(_))) {
            self.pop_share_scope();
            self.emit_defers();
            if matches!(
                f.ret.as_ref(),
                Some(TypeExpr::Named(n))
                    if n == "int"
                        || n == "int64"
                        || n == "int32"
                        || n == "int8"
                        || n == "uint64"
                        || n == "byte"
            ) {
                self.line("return 0;");
            }
        } else if !stmts.iter().any(|s| matches!(s, Stmt::Return(_))) {
            self.pop_share_scope();
            self.emit_defers();
        }
        self.indent = 0;
        self.out.push_str("}\n\n");
    }

    /// Emit `for` / `for … in range …` loops.
    ///
    /// Modes:
    /// - int count (`for i in n` / `for i in range n`): `0 .. n-1`
    /// - array legacy (`for v in arr`): values
    /// - array range index (`for i in range arr`): indices
    /// - array range both (`for i, v in range arr` / `for _, v in range arr`)
    /// - string + `range`: Go-like **runes** (code points); index = byte offset
    /// - string legacy (`for b in s`): bytes
    /// - `for range arr`: side-effect loop, no binders
    fn emit_for(
        &mut self,
        label: Option<&str>,
        binders: &[String],
        is_range: bool,
        iter: &Expr,
        body: &Block,
    ) {
        if let Some(lab) = label {
            self.line(&format!("{lab}: ;"));
        }
        let (ty, val) = match iter {
            Expr::Int(n) => ("int64_t".into(), n.to_string()),
            Expr::Ident(name_iter) => {
                let t = self
                    .locals
                    .get(name_iter)
                    .cloned()
                    .unwrap_or_else(|| "int64_t".into());
                (t, name_iter.clone())
            }
            other => {
                let (t, v) = self.emit_expr(other);
                let tmp = self.fresh("iter");
                self.line(&format!("{t} {tmp} = {v};"));
                (t, tmp)
            }
        };

        if ty == "int64_t" {
            let idx_name = match binders {
                [a] if a != "_" => mangle(a),
                [a, _] if a != "_" => mangle(a),
                _ => self.fresh("_ri"),
            };
            if let [a] = binders {
                if a != "_" {
                    self.locals.insert(a.clone(), "int64_t".into());
                }
            } else if let [a, _] = binders {
                if a != "_" {
                    self.locals.insert(a.clone(), "int64_t".into());
                }
            }
            self.line(&format!(
                "for (int64_t {idx_name} = 0; {idx_name} < {val}; {idx_name}++) {{"
            ));
            self.indent += 1;
            self.push_share_scope();
            for s in &body.stmts {
                self.emit_stmt(s);
            }
            self.pop_share_scope();
            if let Some(lab) = label {
                self.line(&format!("__mako_cont_{lab}: ;"));
            }
            self.indent -= 1;
            self.line("}");
            if let Some(lab) = label {
                self.line(&format!("__mako_break_{lab}: ;"));
            }
            return;
        }

        // Go-like string range: runes (advance cursor before body so continue is safe)
        if ty == "MakoString" && is_range {
            let i = self.fresh("ri");
            let rune = self.fresh("rn");
            let w = self.fresh("rw");
            self.line(&format!("for (size_t {i} = 0; {i} < {val}.len; ) {{"));
            self.indent += 1;
            self.line(&format!("int64_t {rune} = 0;"));
            self.line(&format!(
                "size_t {w} = mako_utf8_decode({val}.data, {val}.len, {i}, &{rune});"
            ));
            match binders {
                [] => {}
                [a] => {
                    if a != "_" {
                        self.line(&format!("int64_t {} = (int64_t){i};", mangle(a)));
                        self.locals.insert(a.clone(), "int64_t".into());
                    }
                }
                [a, b] => {
                    if a != "_" {
                        self.line(&format!("int64_t {} = (int64_t){i};", mangle(a)));
                        self.locals.insert(a.clone(), "int64_t".into());
                    }
                    if b != "_" {
                        self.line(&format!("int64_t {} = {rune};", mangle(b)));
                        self.locals.insert(b.clone(), "int64_t".into());
                    }
                }
                _ => {}
            }
            self.line(&format!("{i} += {w};"));
            for s in &body.stmts {
                self.emit_stmt(s);
            }
            if let Some(lab) = label {
                self.line(&format!("__mako_cont_{lab}: ;"));
            }
            self.indent -= 1;
            self.line("}");
            if let Some(lab) = label {
                self.line(&format!("__mako_break_{lab}: ;"));
            }
            return;
        }

        // Channel range: receive until close (`for v in range ch`)
        if ty == "MakoChan*" && is_range {
            let vtmp = self.fresh("cv");
            let ok = self.fresh("cok");
            self.line(&format!("int64_t {vtmp};"));
            self.line("while (1) {");
            self.indent += 1;
            self.line(&format!(
                "int64_t {ok} = mako_chan_recv_ok({val}, &{vtmp});"
            ));
            self.line(&format!("if (!{ok}) break;"));
            match binders {
                [] => {}
                [a] if a != "_" => {
                    self.line(&format!("int64_t {} = {vtmp};", mangle(a)));
                    self.locals.insert(a.clone(), "int64_t".into());
                }
                _ => {}
            }
            for s in &body.stmts {
                self.emit_stmt(s);
            }
            if let Some(lab) = label {
                self.line(&format!("__mako_cont_{lab}: ;"));
            }
            self.indent -= 1;
            self.line("}");
            if let Some(lab) = label {
                self.line(&format!("__mako_break_{lab}: ;"));
            }
            return;
        }

        // Map range: order unspecified (hash table slot order)
        if matches!(ty.as_str(), "MakoMapSI*" | "MakoMapII*" | "MakoMapSS*") && is_range {
            let i = self.fresh("mi");
            self.line(&format!("for (size_t {i} = 0; {i} < {val}->cap; {i}++) {{"));
            self.indent += 1;
            self.line(&format!(
                "if ({val}->state[{i}] != MAKO_MAP_FULL) continue;"
            ));
            match binders {
                [] => {}
                [a] => {
                    if a != "_" {
                        if ty == "MakoMapSI*" || ty == "MakoMapSS*" {
                            self.line(&format!("MakoString {} = {val}->keys[{i}];", mangle(a)));
                            self.locals.insert(a.clone(), "MakoString".into());
                        } else {
                            self.line(&format!("int64_t {} = {val}->keys[{i}];", mangle(a)));
                            self.locals.insert(a.clone(), "int64_t".into());
                        }
                    }
                }
                [a, b] => {
                    if a != "_" {
                        if ty == "MakoMapSI*" || ty == "MakoMapSS*" {
                            self.line(&format!("MakoString {} = {val}->keys[{i}];", mangle(a)));
                            self.locals.insert(a.clone(), "MakoString".into());
                        } else {
                            self.line(&format!("int64_t {} = {val}->keys[{i}];", mangle(a)));
                            self.locals.insert(a.clone(), "int64_t".into());
                        }
                    }
                    if b != "_" {
                        if ty == "MakoMapSS*" {
                            self.line(&format!("MakoString {} = {val}->vals[{i}];", mangle(b)));
                            self.locals.insert(b.clone(), "MakoString".into());
                        } else {
                            self.line(&format!("int64_t {} = {val}->vals[{i}];", mangle(b)));
                            self.locals.insert(b.clone(), "int64_t".into());
                        }
                    }
                }
                _ => {}
            }
            for s in &body.stmts {
                self.emit_stmt(s);
            }
            if let Some(lab) = label {
                self.line(&format!("__mako_cont_{lab}: ;"));
            }
            self.indent -= 1;
            self.line("}");
            if let Some(lab) = label {
                self.line(&format!("__mako_break_{lab}: ;"));
            }
            return;
        }

        // Slice / byte array / string (legacy byte iteration)
        let i = self.fresh("ri");
        self.line(&format!("for (size_t {i} = 0; {i} < {val}.len; {i}++) {{"));
        self.indent += 1;

        match binders {
            [] => {}
            [a] if is_range => {
                if a != "_" {
                    self.line(&format!("int64_t {} = (int64_t){i};", mangle(a)));
                    self.locals.insert(a.clone(), "int64_t".into());
                }
            }
            [a] => {
                if a != "_" {
                    self.emit_range_value(&ty, &val, &i, a);
                }
            }
            [a, b] => {
                if a != "_" {
                    self.line(&format!("int64_t {} = (int64_t){i};", mangle(a)));
                    self.locals.insert(a.clone(), "int64_t".into());
                }
                if b != "_" {
                    self.emit_range_value(&ty, &val, &i, b);
                }
            }
            _ => {}
        }

        for s in &body.stmts {
            self.emit_stmt(s);
        }
        self.indent -= 1;
        self.line("}");
    }

    /// Render a C-style `for` post clause as a single C expression for the loop
    /// header (so `continue` runs it). Handles the assignment / `i++` forms that a
    /// post is in practice; anything else is emitted as a statement and the header
    /// post is left empty.
    fn post_c_expr(&mut self, post: &Stmt) -> String {
        match post {
            Stmt::Assign { name, value } => {
                let (_, v) = self.emit_expr(value);
                format!("{} = {}", mangle(name), v)
            }
            Stmt::FieldAssign { base, field, value } => {
                let (_, b) = self.emit_expr(base);
                let (_, v) = self.emit_expr(value);
                format!("{b}.{field} = {v}")
            }
            Stmt::Expr(e) => self.emit_expr(e).1,
            other => {
                // Exotic post (rare): emit it and use an empty header post.
                self.emit_stmt(other);
                String::new()
            }
        }
    }

    fn emit_range_value(&mut self, arr_ty: &str, arr: &str, idx: &str, name: &str) {
        // Emit the C variable under its mangled name (so reserved words stay valid),
        // but key `locals` by the raw Mako name for identifier lookups.
        let cname = mangle(name);
        if arr_ty == "MakoByteArray" {
            self.line(&format!(
                "int64_t {cname} = mako_byte_get({arr}, (int64_t){idx});"
            ));
            self.locals.insert(name.to_string(), "int64_t".into());
        } else if arr_ty == "MakoStrArray" {
            self.line(&format!(
                "MakoString {cname} = mako_str_array_get({arr}, (int64_t){idx});"
            ));
            self.locals.insert(name.to_string(), "MakoString".into());
        } else if arr_ty == "MakoFloatArray" {
            self.line(&format!(
                "double {cname} = mako_float_array_get({arr}, (int64_t){idx});"
            ));
            self.locals.insert(name.to_string(), "double".into());
        } else if let Some(sn) = arr_ty.strip_prefix("MakoArr_") {
            self.line(&format!(
                "{sn} {cname} = mako_arr_{sn}_get({arr}, (int64_t){idx});"
            ));
            self.locals.insert(name.to_string(), sn.to_string());
        } else if arr_ty == "MakoString" {
            self.line(&format!(
                "int64_t {cname} = mako_str_get({arr}, (int64_t){idx});"
            ));
            self.locals.insert(name.to_string(), "int64_t".into());
        } else {
            self.line(&format!("int64_t {cname} = {arr}.data[{idx}];"));
            self.locals.insert(name.to_string(), "int64_t".into());
        }
    }

    fn emit_defers(&mut self) {
        let bodies: Vec<Block> = self.defer_stack.iter().rev().cloned().collect();
        for body in bodies {
            for s in &body.stmts {
                self.emit_stmt(s);
            }
        }
    }

    fn emit_stmt(&mut self, stmt: &Stmt) {
        match stmt {
            Stmt::Let {
                name,
                init,
                ty: ann,
                ownership,
                ..
            } => {
                if name == "_" {
                    let (_, val) = self.emit_expr(init);
                    if val != "/*void*/" {
                        self.line(&format!("(void)({val});"));
                    }
                    return;
                }
                // Keep the C variable name consistent with its uses (Ident emission
                // also mangles), so a name colliding with a C keyword stays valid.
                let name = mangle(name);
                let name = &name;
                let as_bytes = matches!(
                    ann,
                    Some(TypeExpr::Array(inner))
                        if matches!(inner.as_ref(), TypeExpr::Named(n) if n == "byte")
                );
                let as_strings = matches!(
                    ann,
                    Some(TypeExpr::Array(inner))
                        if matches!(inner.as_ref(), TypeExpr::Named(n) if n == "string")
                );
                let as_floats = matches!(
                    ann,
                    Some(TypeExpr::Array(inner))
                        if matches!(inner.as_ref(), TypeExpr::Named(n) if n == "float" || n == "float64")
                );
                let as_struct_arr = match ann {
                    Some(TypeExpr::Array(inner)) => match inner.as_ref() {
                        TypeExpr::Named(n) if self.structs.contains_key(n) => Some(n.clone()),
                        _ => None,
                    },
                    _ => None,
                };
                let (ty, val) = if as_bytes {
                    if let Expr::Array(elems) = init {
                        self.emit_byte_array_lit(elems)
                    } else {
                        self.emit_expr(init)
                    }
                } else if as_strings {
                    if let Expr::Array(elems) = init {
                        self.emit_str_array_lit(elems)
                    } else {
                        self.emit_expr(init)
                    }
                } else if as_floats {
                    if let Expr::Array(elems) = init {
                        self.emit_float_array_lit(elems)
                    } else {
                        self.emit_expr(init)
                    }
                } else if let Some(sn) = as_struct_arr {
                    if let Expr::Array(elems) = init {
                        self.emit_struct_array_lit(&sn, elems)
                    } else {
                        self.emit_expr(init)
                    }
                } else if let Expr::Array(elems) = init {
                    // Infer []string / []float / []Struct from literal elements when unannotated.
                    if !elems.is_empty() && elems.iter().all(|e| matches!(e, Expr::String(_))) {
                        self.emit_str_array_lit(elems)
                    } else if !elems.is_empty() && elems.iter().all(|e| matches!(e, Expr::Float(_)))
                    {
                        self.emit_float_array_lit(elems)
                    } else if !elems.is_empty()
                        && elems.iter().all(|e| struct_lit_name(e).is_some())
                    {
                        if let Some(name) = struct_lit_name(&elems[0]) {
                            let sn = name.to_string();
                            self.emit_struct_array_lit(&sn, elems)
                        } else {
                            self.emit_expr(init)
                        }
                    } else {
                        self.emit_expr(init)
                    }
                } else {
                    self.emit_expr(init)
                };
                let ty = if ty == "/*auto*/" {
                    "int64_t".into()
                } else {
                    ty
                };
                let (ty, val) = if let Some(TypeExpr::Named(iname)) = ann {
                    if self.interfaces.iter().any(|(n, _)| n == iname) {
                        let iface_ty = format!("MakoIface_{iname}");
                        if ty != iface_ty {
                            let concrete = ty.clone();
                            let box_fn = format!("mako_iface_{iname}_from_{concrete}");
                            let tmp = self.fresh("ibox");
                            self.line(&format!("{iface_ty} {tmp} = {box_fn}({val});"));
                            (iface_ty, tmp)
                        } else {
                            (ty, val)
                        }
                    } else {
                        (ty, val)
                    }
                } else {
                    (ty, val)
                };
                // Propagate ptr-chan element type from rhs temporary
                if ty == "MakoChanPtr*" {
                    if let Some(st) = self.chan_ptr_elems.get(&val).cloned() {
                        self.chan_ptr_elems.insert(name.clone(), st);
                    }
                }
                if self.chan_float.contains(&val) {
                    self.chan_float.insert(name.clone());
                }
                if ty == "MakoTask*" {
                    if let Some(rt) = self.job_rets.get(&val).cloned() {
                        self.job_rets.insert(name.clone(), rt);
                    }
                    if let Some(ok) = self.job_ok_kinds.get(&val).cloned() {
                        self.job_ok_kinds.insert(name.clone(), ok);
                    }
                }
                self.locals.insert(name.clone(), ty.clone());
                // Propagate Result Err enum type from call
                if ty == "MakoResultInt" {
                    if let Expr::Call { callee, .. } = init {
                        if let Expr::Ident(fname) = callee.as_ref() {
                            if let Some(ec) = self.fn_result_err_enum.get(fname).cloned() {
                                self.result_err_enums.insert(name.clone(), ec);
                            }
                            if let Some(ok) = self.fn_result_ok_kind.get(fname).cloned() {
                                self.result_ok_kinds.insert(name.clone(), ok);
                            }
                            if let Some(sn) = self.fn_result_ok_struct.get(fname).cloned() {
                                self.result_ok_structs.insert(name.clone(), sn);
                            }
                        }
                    }
                }
                self.line(&format!("{ty} {name} = {val};"));
                if *ownership == Ownership::Share {
                    self.register_share_local(name);
                }
            }
            Stmt::LetMulti { names, init, .. } => {
                let (ty, val) = self.emit_expr(init);
                let tmp = self.fresh("mr");
                self.line(&format!("{ty} {tmp} = {val};"));
                // Unpack MakoTup_* fields — declare new locals or assign existing
                let tags: Vec<&str> = ty
                    .strip_prefix("MakoTup_")
                    .unwrap_or("")
                    .split('_')
                    .filter(|s| !s.is_empty())
                    .collect();
                for (i, n) in names.iter().enumerate() {
                    if n == "_" {
                        continue;
                    }
                    let tag = tags.get(i).copied().unwrap_or("int");
                    let cty = match tag {
                        "string" => "MakoString",
                        "float" => "double",
                        "bool" => "bool",
                        _ => "int64_t",
                    };
                    if self.locals.contains_key(n) {
                        // Reassignment: `a, b = f()`
                        self.line(&format!("{} = {tmp}._{i};", mangle(n)));
                    } else {
                        self.locals.insert(n.clone(), cty.into());
                        self.line(&format!("{cty} {} = {tmp}._{i};", mangle(n)));
                    }
                }
            }
            Stmt::LetCommaOk {
                value,
                ok,
                base,
                index,
                ..
            } => {
                let (bty, b) = self.emit_expr(base);
                let (_, i) = self.emit_expr(index);
                let (vty, get_fn, has_fn) = match bty.as_str() {
                    "MakoMapSI*" => ("int64_t", "mako_map_si_get", "mako_map_si_has"),
                    "MakoMapII*" => ("int64_t", "mako_map_ii_get", "mako_map_ii_has"),
                    "MakoMapSS*" => ("MakoString", "mako_map_ss_get", "mako_map_ss_has"),
                    other => {
                        self.line(&format!("/* comma-ok unsupported map type {other} */"));
                        return;
                    }
                };
                if value != "_" {
                    self.line(&format!("{vty} {} = {get_fn}({b}, {i});", mangle(value)));
                    self.locals.insert(value.clone(), vty.into());
                } else {
                    self.line(&format!("(void){get_fn}({b}, {i});"));
                }
                if ok != "_" {
                    self.line(&format!("bool {} = {has_fn}({b}, {i});", mangle(ok)));
                    self.locals.insert(ok.clone(), "bool".into());
                }
            }
            Stmt::Assign { name, value } => {
                if name == "_" {
                    let (_, val) = self.emit_expr(value);
                    if val != "/*void*/" {
                        self.line(&format!("(void)({val});"));
                    }
                    return;
                }
                let (vty, val) = self.emit_expr(value);
                let val = if let Some(exp) = self.locals.get(name).cloned() {
                    if exp.starts_with("MakoIface_") && vty != exp {
                        let iname = &exp["MakoIface_".len()..];
                        let box_fn = format!("mako_iface_{iname}_from_{vty}");
                        let tmp = self.fresh("ibox");
                        self.line(&format!("{exp} {tmp} = {box_fn}({val});"));
                        tmp
                    } else {
                        val
                    }
                } else {
                    val
                };
                self.line(&format!("{} = {val};", mangle(name)));
            }
            Stmt::IndexAssign { base, index, value } => {
                let (bty, b) = self.emit_expr(base);
                let (_, i) = self.emit_expr(index);
                let (_, v) = self.emit_expr(value);
                if bty == "MakoMapSI*" {
                    self.line(&format!("mako_map_si_set({b}, {i}, {v});"));
                } else if bty == "MakoMapII*" {
                    self.line(&format!("mako_map_ii_set({b}, {i}, {v});"));
                } else if bty == "MakoMapSS*" {
                    self.line(&format!("mako_map_ss_set({b}, {i}, {v});"));
                } else if bty == "MakoByteArray" {
                    let tmp = self.fresh("iass");
                    self.line(&format!("int64_t {tmp} = {i};"));
                    self.line(&format!("mako_byte_set({b}, {tmp}, {v});"));
                } else if bty == "MakoStrArray" {
                    let tmp = self.fresh("iass");
                    self.line(&format!("int64_t {tmp} = {i};"));
                    self.line(&format!("mako_str_array_set({b}, {tmp}, {v});"));
                } else if bty == "MakoFloatArray" {
                    let tmp = self.fresh("iass");
                    self.line(&format!("int64_t {tmp} = {i};"));
                    self.line(&format!("mako_float_array_set({b}, {tmp}, {v});"));
                } else if let Some(sn) = bty.strip_prefix("MakoArr_") {
                    let tmp = self.fresh("iass");
                    self.line(&format!("int64_t {tmp} = {i};"));
                    self.line(&format!("mako_arr_{sn}_set({b}, {tmp}, {v});"));
                } else {
                    let tmp = self.fresh("iass");
                    self.line(&format!("int64_t {tmp} = {i};"));
                    self.emit_bounds_check(
                        &format!("{tmp} < 0 || (size_t){tmp} >= {b}.len"),
                        "index out of bounds",
                    );
                    self.line(&format!("{b}.data[{tmp}] = {v};"));
                }
            }
            Stmt::FieldAssign { base, field, value } => {
                let (bty, b) = self.emit_expr(base);
                let (_, v) = self.emit_expr(value);
                let _ = bty;
                self.line(&format!("{b}.{field} = {v};"));
            }
            Stmt::Expr(e) => {
                let (_, val) = self.emit_expr(e);
                if val != "/*void*/" {
                    self.line(&format!("{val};"));
                }
            }
            Stmt::Return(None) => {
                // Drop all nested share scopes before leaving the function.
                while !self.share_scopes.is_empty() {
                    self.pop_share_scope();
                }
                self.emit_defers();
                self.line("return;");
            }
            Stmt::Return(Some(e)) => {
                let (_, val) = self.emit_expr(e);
                while !self.share_scopes.is_empty() {
                    self.pop_share_scope();
                }
                self.emit_defers();
                self.line(&format!("return {val};"));
            }
            Stmt::Defer { body } => {
                self.defer_stack.push(body.clone());
            }
            Stmt::If {
                init,
                cond,
                then_block,
                else_block,
            } => {
                // `if init; cond { … }`: run the init in a fresh C scope so its
                // binding is confined to this if/else (and cannot collide with a
                // sibling `if x := …` in the same block).
                if let Some(init) = init {
                    self.line("{");
                    self.indent += 1;
                    self.emit_stmt(init);
                }
                let (_, c) = self.emit_expr(cond);
                self.line(&format!("if ({c}) {{"));
                self.indent += 1;
                self.push_share_scope();
                for s in &then_block.stmts {
                    self.emit_stmt(s);
                }
                self.pop_share_scope();
                self.indent -= 1;
                if let Some(eb) = else_block {
                    self.line("} else {");
                    self.indent += 1;
                    self.push_share_scope();
                    for s in &eb.stmts {
                        self.emit_stmt(s);
                    }
                    self.pop_share_scope();
                    self.indent -= 1;
                }
                self.line("}");
                if init.is_some() {
                    self.indent -= 1;
                    self.line("}");
                }
            }
            Stmt::While { label, cond, body } => {
                if let Some(lab) = label {
                    self.line(&format!("{lab}: while (1) {{"));
                } else {
                    self.line("while (1) {");
                }
                self.indent += 1;
                let (_, c) = self.emit_expr(cond);
                self.line(&format!("if (!({c})) break;"));
                self.push_share_scope();
                for s in &body.stmts {
                    self.emit_stmt(s);
                }
                self.pop_share_scope();
                if let Some(lab) = label {
                    self.line(&format!("__mako_cont_{lab}: ;"));
                }
                self.indent -= 1;
                self.line("}");
                if let Some(lab) = label {
                    self.line(&format!("__mako_break_{lab}: ;"));
                }
            }
            Stmt::Break(None) => self.line("break;"),
            Stmt::Break(Some(lab)) => self.line(&format!("goto __mako_break_{lab};")),
            Stmt::Continue(None) => self.line("continue;"),
            Stmt::Continue(Some(lab)) => self.line(&format!("goto __mako_cont_{lab};")),
            Stmt::For {
                label,
                binders,
                is_range,
                iter,
                body,
            } => {
                self.emit_for(label.as_deref(), binders, *is_range, iter, body);
            }
            Stmt::CFor {
                label,
                init,
                cond,
                post,
                body,
            } => {
                // Lower to a real C `for (;; post)` inside a scope for `init`. The
                // condition is re-checked at the top of the body (so it may use
                // temporaries); `post` sits in the header so C's own `continue`
                // runs it.
                self.line("{");
                self.indent += 1;
                self.emit_stmt(init);
                let post_str = self.post_c_expr(post);
                self.line(&format!("for (;; {post_str}) {{"));
                self.indent += 1;
                let (_, c) = self.emit_expr(cond);
                self.line(&format!("if (!({c})) break;"));
                self.push_share_scope();
                for s in &body.stmts {
                    self.emit_stmt(s);
                }
                self.pop_share_scope();
                if let Some(lab) = label {
                    self.line(&format!("__mako_cont_{lab}: ;"));
                }
                self.indent -= 1;
                self.line("}");
                if let Some(lab) = label {
                    self.line(&format!("__mako_break_{lab}: ;"));
                }
                self.indent -= 1;
                self.line("}");
            }
            Stmt::Crew { name, body } => {
                self.locals.insert(name.clone(), "MakoNursery".into());
                self.line(&format!("MakoNursery {name} = mako_nursery_new();"));
                for s in &body.stmts {
                    self.emit_stmt(s);
                }
                self.line(&format!("mako_nursery_cancel_join(&{name});"));
            }
            Stmt::Arena { name, body } => {
                self.locals.insert(name.clone(), "MakoArena".into());
                self.line(&format!("MakoArena {name} = mako_arena_new();"));
                let prev = self.current_arena.replace(name.clone());
                for s in &body.stmts {
                    self.emit_stmt(s);
                }
                self.current_arena = prev;
                self.line(&format!("mako_arena_free(&{name});"));
            }
            Stmt::Unsafe { body } => {
                self.line("/* unsafe: bounds checks elided (debug) */");
                self.unsafe_depth += 1;
                for s in &body.stmts {
                    self.emit_stmt(s);
                }
                self.unsafe_depth -= 1;
            }
            Stmt::Select {
                timeout_ms,
                arms,
                default_arm,
            } => {
                let (_, ms) = self.emit_expr(timeout_ms);
                let which = self.fresh("sel");
                let arr = self.fresh("selchs");
                let n = arms.len();
                let all_str = arms.iter().all(|(ch, _)| {
                    self.locals
                        .get(ch)
                        .map(|t| t.as_str() == "MakoChanStr*")
                        .unwrap_or(false)
                });
                let all_ptr = arms.iter().all(|(ch, _)| {
                    self.locals
                        .get(ch)
                        .map(|t| t.as_str() == "MakoChanPtr*")
                        .unwrap_or(false)
                });
                if all_str {
                    self.line(&format!("MakoChanStr *{arr}[{n}];"));
                    for (i, (ch, _)) in arms.iter().enumerate() {
                        self.line(&format!("{arr}[{i}] = {ch};"));
                    }
                    self.line(&format!(
                        "int64_t {which} = mako_chan_str_selectn({arr}, {n}, {ms});"
                    ));
                } else if all_ptr {
                    self.line(&format!("MakoChanPtr *{arr}[{n}];"));
                    for (i, (ch, _)) in arms.iter().enumerate() {
                        self.line(&format!("{arr}[{i}] = {ch};"));
                    }
                    self.line(&format!(
                        "int64_t {which} = mako_chan_ptr_selectn({arr}, {n}, {ms});"
                    ));
                } else {
                    self.line(&format!("MakoChan *{arr}[{n}];"));
                    for (i, (ch, _)) in arms.iter().enumerate() {
                        self.line(&format!("{arr}[{i}] = {ch};"));
                    }
                    self.line(&format!(
                        "int64_t {which} = mako_chan_selectn({arr}, {n}, {ms});"
                    ));
                }
                for (i, (_ch, body)) in arms.iter().enumerate() {
                    if i == 0 {
                        self.line(&format!("if ({which} == {i}) {{"));
                    } else {
                        self.line(&format!("}} else if ({which} == {i}) {{"));
                    }
                    self.indent += 1;
                    for s in &body.stmts {
                        self.emit_stmt(s);
                    }
                    self.indent -= 1;
                }
                if let Some(def) = default_arm {
                    self.line(&format!("}} else if ({which} < 0) {{"));
                    self.indent += 1;
                    for s in &def.stmts {
                        self.emit_stmt(s);
                    }
                    self.indent -= 1;
                }
                self.line("}");
            }
        }
    }

    /// Returns (c_type, c_expr)
    fn emit_expr(&mut self, expr: &Expr) -> (String, String) {
        match expr {
            Expr::Int(n) => ("int64_t".into(), n.to_string()),
            Expr::Float(n) => ("double".into(), format!("{n}")),
            Expr::Bool(b) => ("bool".into(), if *b { "true" } else { "false" }.into()),
            Expr::String(s) => {
                let escaped = escape_c(s);
                (
                    "MakoString".into(),
                    format!("mako_str_from_cstr(\"{escaped}\")"),
                )
            }
            Expr::Ident(n) => {
                if n == "None" {
                    return ("MakoOptionInt".into(), "mako_none_int()".into());
                }
                if let Some(v) = self.const_ints.get(n) {
                    return ("int64_t".into(), v.to_string());
                }
                // Unit enum variant used as value
                if let Some(enum_name) = self.variant_to_enum.get(n).cloned() {
                    if let Some(info) = self.enums.get(&enum_name).cloned() {
                        if let Some((tag, fields)) = info.variants.get(n) {
                            if fields.is_empty() {
                                let tag = *tag;
                                let c_name = info.c_name.clone();
                                let tmp = self.fresh("ev");
                                self.line(&format!("{c_name} {tmp};"));
                                self.line(&format!("{tmp}.tag = {tag};"));
                                self.line(&format!("{tmp}.i0 = 0;"));
                                self.line(&format!("{tmp}.s0 = (MakoString){{NULL, 0}};"));
                                return (c_name, tmp);
                            }
                        }
                    }
                }
                let ty = self
                    .locals
                    .get(n)
                    .cloned()
                    .unwrap_or_else(|| "int64_t".into());
                (ty, mangle(n))
            }
            Expr::Binary { op, left, right } => {
                if matches!(op, BinOp::And | BinOp::Or) {
                    let (_, lv) = self.emit_expr(left);
                    let tmp = self.fresh(if *op == BinOp::And { "and" } else { "or" });
                    if *op == BinOp::And {
                        self.line(&format!("bool {tmp} = false;"));
                        self.line(&format!("if ({lv}) {{"));
                    } else {
                        self.line(&format!("bool {tmp} = true;"));
                        self.line(&format!("if (!({lv})) {{"));
                    }
                    self.indent += 1;
                    let (_, rv) = self.emit_expr(right);
                    self.line(&format!("{tmp} = {rv};"));
                    self.indent -= 1;
                    self.line("}");
                    return ("bool".into(), tmp);
                }
                let (lt, lv) = self.emit_expr(left);
                let (rt, rv) = self.emit_expr(right);
                if *op == BinOp::Add && lt == "MakoString" {
                    let tmp = self.fresh("s");
                    self.line(&format!("MakoString {tmp} = mako_str_concat({lv}, {rv});"));
                    return ("MakoString".into(), tmp);
                }
                if (*op == BinOp::Eq || *op == BinOp::Ne)
                    && lt == "MakoString"
                    && rt == "MakoString"
                {
                    let eq = format!("mako_str_eq({lv}, {rv})");
                    return if *op == BinOp::Eq {
                        ("bool".into(), eq)
                    } else {
                        ("bool".into(), format!("(!{eq})"))
                    };
                }
                if *op == BinOp::BitClear {
                    let ty = if lt == "double" {
                        "int64_t"
                    } else {
                        lt.as_str()
                    };
                    return (ty.into(), format!("(({lv}) & ~({rv}))"));
                }
                let ty = match op {
                    BinOp::Eq
                    | BinOp::Ne
                    | BinOp::Lt
                    | BinOp::Le
                    | BinOp::Gt
                    | BinOp::Ge
                    | BinOp::And
                    | BinOp::Or => "bool",
                    _ => {
                        if lt == "double" {
                            "double"
                        } else {
                            "int64_t"
                        }
                    }
                };
                let _ = rt;
                // Checked integer arithmetic when trap mode is on (float always raw).
                if ty == "int64_t" {
                    let op_ch = match op {
                        BinOp::Add => Some('+'),
                        BinOp::Sub => Some('-'),
                        BinOp::Mul => Some('*'),
                        _ => None,
                    };
                    if let Some(ch) = op_ch {
                        if let Some(fn_name) = self.overflow_mode.trap_fn(ch) {
                            return (ty.into(), format!("{fn_name}({lv}, {rv})"));
                        }
                    }
                }
                let cop = match op {
                    BinOp::Add => "+",
                    BinOp::Sub => "-",
                    BinOp::Mul => "*",
                    BinOp::Div => "/",
                    BinOp::Mod => "%",
                    BinOp::Eq => "==",
                    BinOp::Ne => "!=",
                    BinOp::Lt => "<",
                    BinOp::Le => "<=",
                    BinOp::Gt => ">",
                    BinOp::Ge => ">=",
                    BinOp::And | BinOp::Or => unreachable!("short-circuit handled above"),
                    BinOp::BitAnd => "&",
                    BinOp::BitOr => "|",
                    BinOp::BitXor => "^",
                    BinOp::BitClear => unreachable!(),
                    BinOp::Shl => "<<",
                    BinOp::Shr => ">>",
                };
                (ty.into(), format!("({lv} {cop} {rv})"))
            }
            Expr::Unary { op, expr } => {
                let (ty, v) = self.emit_expr(expr);
                match op {
                    UnaryOp::Neg => (ty, format!("(-{v})")),
                    UnaryOp::Not => ("bool".into(), format!("(!{v})")),
                    UnaryOp::BitNot => (ty, format!("(~{v})")),
                }
            }
            Expr::Call { callee, args } => {
                if let Expr::Ident(name) = callee.as_ref() {
                    // const fn: fold when all args are const-foldable
                    if self.const_fns.contains_key(name) {
                        if let Some(v) = fold_const_c_env(
                            &Expr::Call {
                                callee: callee.clone(),
                                args: args.clone(),
                            },
                            &self.const_ints,
                            &self.const_fns,
                        ) {
                            return ("int64_t".into(), v.to_string());
                        }
                    }
                    // Track Result[T, Enum] err type for match Err(e)
                    if let Some(ec) = self.fn_result_err_enum.get(name).cloned() {
                        // Will attach to the returned temp if caller binds it via Let —
                        // also attach to any fresh temp created below by recording after emit.
                        let _ = ec; // used in Let path via fn_result_err_enum lookup
                    }
                    match name.as_str() {
                        "print" => {
                            let (ty, v) = self.emit_expr(&args[0]);
                            match ty.as_str() {
                                "MakoString" => {
                                    self.line(&format!("mako_print_str({v});"));
                                }
                                "int64_t" | "/*auto*/" => {
                                    self.line(&format!("mako_print_int({v});"));
                                }
                                "bool" => {
                                    self.line(&format!("mako_print_bool({v});"));
                                }
                                "double" => {
                                    self.line(&format!("mako_print_float({v});"));
                                }
                                _ => {
                                    self.line(&format!("mako_print_int((int64_t){v});"));
                                }
                            }
                            return ("void".into(), "/*void*/".into());
                        }
                        "print_int" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_print_int({v});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "print_int64" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_print_int({v});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "print_int32" | "print_int8" | "print_uint64" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_print_int({v});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "print_float" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_print_float({v});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "int64" | "int32" | "int" | "byte" => {
                            let (ty, v) = self.emit_expr(&args[0]);
                            if ty == "double" {
                                return ("int64_t".into(), format!("(int64_t)({v})"));
                            }
                            return ("int64_t".into(), v);
                        }
                        "int8" => {
                            let (ty, v) = self.emit_expr(&args[0]);
                            let as_i = if ty == "double" {
                                format!("(int64_t)({v})")
                            } else {
                                v
                            };
                            return ("int64_t".into(), format!("mako_to_int8({as_i})"));
                        }
                        "uint64" => {
                            let (ty, v) = self.emit_expr(&args[0]);
                            let as_i = if ty == "double" {
                                format!("(int64_t)({v})")
                            } else {
                                v
                            };
                            return (
                                "int64_t".into(),
                                format!("mako_to_uint64_from_signed({as_i})"),
                            );
                        }
                        "float" | "float64" => {
                            let (ty, v) = self.emit_expr(&args[0]);
                            if ty == "double" {
                                return ("double".into(), v);
                            }
                            return ("double".into(), format!("(double)({v})"));
                        }
                        "bytes" => {
                            let (ty, v) = self.emit_expr(&args[0]);
                            if ty == "MakoByteArray" {
                                return (ty, v);
                            }
                            let tmp = self.fresh("by");
                            self.line(&format!(
                                "MakoByteArray {tmp} = mako_bytes_from_string({v});"
                            ));
                            return ("MakoByteArray".into(), tmp);
                        }
                        "string" => {
                            let (ty, v) = self.emit_expr(&args[0]);
                            if ty == "MakoString" {
                                return (ty, v);
                            }
                            if ty == "MakoByteArray" {
                                let tmp = self.fresh("sb");
                                self.line(&format!(
                                    "MakoString {tmp} = mako_string_from_bytes({v});"
                                ));
                                return ("MakoString".into(), tmp);
                            }
                            // int family → decimal string
                            return ("MakoString".into(), format!("mako_int_to_string({v})"));
                        }
                        "Ok" => {
                            let (vty, v) = self.emit_expr(&args[0]);
                            if vty == "MakoString" {
                                return ("MakoResultInt".into(), format!("mako_ok_str({v})"));
                            }
                            if vty == "double" {
                                return (
                                    "MakoResultInt".into(),
                                    format!("mako_ok_float_res({v})"),
                                );
                            }
                            // Named struct Ok: heap-box and pack pointer in value.
                            if self.structs.contains_key(&vty)
                                || self.structs.values().any(|s| s.c_name == vty)
                            {
                                let boxn = self.fresh("okbox");
                                self.line(&format!(
                                    "{vty} *{boxn} = ({vty}*)malloc(sizeof({vty}));"
                                ));
                                self.line(&format!("*{boxn} = {v};"));
                                return (
                                    "MakoResultInt".into(),
                                    format!("mako_ok_ptr((void*){boxn})"),
                                );
                            }
                            // []int Ok: box the array
                            if vty == "MakoIntArray" {
                                let boxn = self.fresh("iabox");
                                self.line(&format!(
                                    "MakoIntArray *{boxn} = (MakoIntArray*)malloc(sizeof(MakoIntArray));"
                                ));
                                self.line(&format!("*{boxn} = {v};"));
                                return (
                                    "MakoResultInt".into(),
                                    format!("mako_ok_ptr((void*){boxn})"),
                                );
                            }
                            // map Ok: pass map pointer (owned by Result)
                            if matches!(
                                vty.as_str(),
                                "MakoMapSI*" | "MakoMapII*" | "MakoMapSS*"
                            ) {
                                return (
                                    "MakoResultInt".into(),
                                    format!("mako_ok_ptr((void*){v})"),
                                );
                            }
                            return (
                                "MakoResultInt".into(),
                                format!("mako_ok_int((int64_t)({v}))"),
                            );
                        }
                        "Err" => {
                            let (ty, v) = self.emit_expr(&args[0]);
                            if ty.starts_with("MakoEnum_") {
                                let tmp = self.fresh("erre");
                                self.line(&format!(
                                    "MakoResultInt {tmp} = mako_err_enum_ex((int)({v}).tag, ({v}).i0, ({v}).i1, ({v}).i2, ({v}).s0, ({v}).s1);"
                                ));
                                return ("MakoResultInt".into(), tmp);
                            }
                            // string (or coerce) error
                            if ty != "MakoString" {
                                // int tag as synthetic enum-less error via string
                                let tmp = self.fresh("errs");
                                self.line(&format!(
                                    "MakoResultInt {tmp} = mako_err_int(mako_int_to_string((int64_t)({v})));"
                                ));
                                return ("MakoResultInt".into(), tmp);
                            }
                            return ("MakoResultInt".into(), format!("mako_err_int({v})"));
                        }
                        "error" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            return ("MakoResultInt".into(), format!("mako_err_int({v})"));
                        }
                        "wrap_err" | "error_context" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let (_, msg) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("we");
                            self.line(&format!("MakoResultInt {tmp} = mako_wrap_err({r}, {msg});"));
                            return ("MakoResultInt".into(), tmp);
                        }
                        "error_join" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ej");
                            self.line(&format!(
                                "MakoResultInt {tmp} = mako_error_join({a}, {b});"
                            ));
                            return ("MakoResultInt".into(), tmp);
                        }
                        "error_tag" => {
                            let (_, tag) = self.emit_expr(&args[0]);
                            let (_, msg) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("et");
                            self.line(&format!(
                                "MakoResultInt {tmp} = mako_error_tag({tag}, {msg});"
                            ));
                            return ("MakoResultInt".into(), tmp);
                        }
                        "errorf" => {
                            let (_, fmt) = self.emit_expr(&args[0]);
                            let (_, arg) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ef");
                            self.line(&format!("MakoResultInt {tmp} = mako_errorf({fmt}, {arg});"));
                            return ("MakoResultInt".into(), tmp);
                        }
                        "error_is" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            return ("bool".into(), format!("(mako_error_is({r}, {n}) != 0)"));
                        }
                        "error_string" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("es");
                            self.line(&format!("MakoString {tmp} = mako_error_string({r});"));
                            return ("MakoString".into(), tmp);
                        }
                        "dbg" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let expr_lit = match &args[0] {
                                Expr::Ident(n) => n.as_str(),
                                _ => "expr",
                            };
                            let tmp = self.fresh("dbg");
                            self.line(&format!(
                                "int64_t {tmp} = mako_dbg_int(__FILE__, __LINE__, \"{expr_lit}\", {v});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "dbg_str" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let expr_lit = match &args[0] {
                                Expr::Ident(n) => n.as_str(),
                                _ => "expr",
                            };
                            let tmp = self.fresh("dbgs");
                            self.line(&format!(
                                "MakoString {tmp} = mako_dbg_str(__FILE__, __LINE__, \"{expr_lit}\", {v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "Some" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            return ("MakoOptionInt".into(), format!("mako_some_int({v})"));
                        }
                        "None" => {
                            return ("MakoOptionInt".into(), "mako_none_int()".into());
                        }
                        "assert" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_assert({v});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "assert_eq" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_assert_eq({a}, {b});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "assert_eq_str" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_assert_eq_str({a}, {b});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "fail" if args.len() == 1 => {
                            let (_, m) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_fail({m});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "t_run" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_t_run({m});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "t_run_nested" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_t_run_nested({m});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "result_unwrap_or" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let (_, fb) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("u");
                            self.line(&format!("int64_t {tmp} = ({r}.ok ? {r}.value : {fb});"));
                            return ("int64_t".into(), tmp);
                        }
                        "str_len" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_str_len({v})"));
                        }
                        "rune_count" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_rune_count({v})"));
                        }
                        "str_builder" => {
                            let tmp = self.fresh("sb");
                            self.line(&format!("MakoStrBuilder *{tmp} = mako_str_builder_new();"));
                            return ("MakoStrBuilder*".into(), tmp);
                        }
                        "builder_write" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_str_builder_write({b}, {s});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "builder_write_byte" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_str_builder_write_byte({b}, {v});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "builder_string" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("s");
                            self.line(&format!("MakoString {tmp} = mako_str_builder_string({b});"));
                            return ("MakoString".into(), tmp);
                        }
                        "builder_len" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_str_builder_len({b})"));
                        }
                        "uuid_v4" => {
                            let tmp = self.fresh("u");
                            self.line(&format!("MakoUuid {tmp} = mako_uuid_v4();"));
                            return ("MakoUuid".into(), tmp);
                        }
                        "uuid_nil" => {
                            let tmp = self.fresh("u");
                            self.line(&format!("MakoUuid {tmp} = mako_uuid_nil();"));
                            return ("MakoUuid".into(), tmp);
                        }
                        "uuid_string" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("us");
                            self.line(&format!("MakoString {tmp} = mako_uuid_string({u});"));
                            return ("MakoString".into(), tmp);
                        }
                        "uuid_parse" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("up");
                            self.line(&format!("MakoUuid {tmp} = mako_uuid_parse({s}, NULL);"));
                            return ("MakoUuid".into(), tmp);
                        }
                        "uuid_parse_ok" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("bool".into(), format!("mako_uuid_parse_ok({s})"));
                        }
                        "uuid_eq" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return ("bool".into(), format!("mako_uuid_eq({a}, {b})"));
                        }
                        "uuid_is_nil" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            return ("bool".into(), format!("mako_uuid_is_nil({u})"));
                        }
                        "uuid_check" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("uc");
                            self.line(&format!("MakoResultInt {tmp} = mako_uuid_check({s});"));
                            return ("MakoResultInt".into(), tmp);
                        }
                        "str_eq" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return ("bool".into(), format!("mako_str_eq({a}, {b})"));
                        }
                        "str_contains" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return ("bool".into(), format!("mako_str_contains({a}, {b})"));
                        }
                        "str_has_prefix" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return (
                                "bool".into(),
                                format!("(bool)mako_str_has_prefix({a}, {b})"),
                            );
                        }
                        "str_has_suffix" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return (
                                "bool".into(),
                                format!("(bool)mako_str_has_suffix({a}, {b})"),
                            );
                        }
                        "str_index" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_str_index({a}, {b})"));
                        }
                        "str_last_index" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_str_last_index({a}, {b})"));
                        }
                        "str_trim" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tr");
                            self.line(&format!("MakoString {tmp} = mako_str_trim({a}, {b});"));
                            return ("MakoString".into(), tmp);
                        }
                        "str_trim_space" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("trs");
                            self.line(&format!("MakoString {tmp} = mako_str_trim_space({a});"));
                            return ("MakoString".into(), tmp);
                        }
                        "str_trim_left" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("trl");
                            self.line(&format!("MakoString {tmp} = mako_str_trim_left({a}, {b});"));
                            return ("MakoString".into(), tmp);
                        }
                        "str_trim_right" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("trr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_str_trim_right({a}, {b});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "str_to_lower" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("stl");
                            self.line(&format!("MakoString {tmp} = mako_str_to_lower({a});"));
                            return ("MakoString".into(), tmp);
                        }
                        "str_to_upper" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("stu");
                            self.line(&format!("MakoString {tmp} = mako_str_to_upper({a});"));
                            return ("MakoString".into(), tmp);
                        }
                        "str_repeat" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("srp");
                            self.line(&format!("MakoString {tmp} = mako_str_repeat({a}, {n});"));
                            return ("MakoString".into(), tmp);
                        }
                        "str_replace" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, o) = self.emit_expr(&args[1]);
                            let (_, n) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("srep");
                            self.line(&format!(
                                "MakoString {tmp} = mako_str_replace({s}, {o}, {n});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "str_split" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, sep) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ssp");
                            self.line(&format!("MakoStrArray {tmp} = mako_str_split({s}, {sep});"));
                            return ("MakoStrArray".into(), tmp);
                        }
                        "str_fields" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("sf");
                            self.line(&format!("MakoStrArray {tmp} = mako_str_fields({s});"));
                            return ("MakoStrArray".into(), tmp);
                        }
                        "str_join" => {
                            let (_, parts) = self.emit_expr(&args[0]);
                            let (_, sep) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("sj");
                            self.line(&format!(
                                "MakoString {tmp} = mako_str_join({parts}, {sep});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "int_to_string" | "format_int" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            return ("MakoString".into(), format!("mako_int_to_string({v})"));
                        }
                        "format_float" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ff");
                            self.line(&format!("MakoString {tmp} = mako_format_float({v}, {p});"));
                            return ("MakoString".into(), tmp);
                        }
                        "format_bool" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("fb");
                            self.line(&format!(
                                "MakoString {tmp} = mako_format_bool({v} ? 1 : 0);"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "parse_bool" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pb");
                            self.line(&format!("MakoResultInt {tmp} = mako_parse_bool({s});"));
                            return ("MakoResultInt".into(), tmp);
                        }
                        "fmt_sprintf" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, a) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("fs");
                            self.line(&format!("MakoString {tmp} = mako_fmt_sprintf_s({f}, {a});"));
                            return ("MakoString".into(), tmp);
                        }
                        "fmt_sprintf_d" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, a) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("fd");
                            self.line(&format!("MakoString {tmp} = mako_fmt_sprintf_d({f}, {a});"));
                            return ("MakoString".into(), tmp);
                        }
                        "hex_encode" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("he");
                            self.line(&format!("MakoString {tmp} = mako_hex_encode({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "arena_text" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("at");
                            self.line(&format!("MakoString {tmp} = mako_arena_text(&{a}, {s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "arena_ints" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ai");
                            self.line(&format!("MakoIntArray {tmp} = mako_arena_ints(&{a}, {n});"));
                            return ("MakoIntArray".into(), tmp);
                        }
                        "arena_stamp" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_arena_stamp(&{a}, {v})"));
                        }
                        "http_serve" => {
                            let (_, port) = self.emit_expr(&args[0]);
                            let (_, body) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("hs");
                            self.line(&format!("int64_t {tmp} = mako_http_serve({port}, {body});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http_echo" => {
                            let (_, port) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("he");
                            self.line(&format!("int64_t {tmp} = mako_http_echo({port});"));
                            return ("int64_t".into(), tmp);
                        }
                        "chan_new" => {
                            let (_, cap) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ch");
                            self.line(&format!("MakoChan *{tmp} = mako_chan_new({cap});"));
                            return ("MakoChan*".into(), tmp);
                        }
                        "share_of" => {
                            // Backward-compatible alias: share_of(int) → share_int
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("sh");
                            self.line(&format!("MakoShareInt {tmp} = mako_share_int({v});"));
                            return ("MakoShareInt".into(), tmp);
                        }
                        "chan_try_send" => {
                            let (_, ch) = self.emit_expr(&args[0]);
                            let (_, value) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_chan_try_send({ch}, {value})"),
                            );
                        }
                        "chan_len" => {
                            let (_, ch) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_chan_len({ch})"));
                        }
                        "chan_cap" => {
                            let (_, ch) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_chan_cap({ch})"));
                        }
                        "sleep_ms" | "time_sleep_ms" => {
                            let (_, ms) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_sleep_ms({ms});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "elapsed_ms" => {
                            let (_, start) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("el");
                            self.line(&format!("int64_t {tmp} = mako_now_ms() - ({start});"));
                            return ("int64_t".into(), tmp);
                        }
                        "exit" => {
                            let (_, code) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_exit({code});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "now_ms" => {
                            let tmp = self.fresh("now");
                            self.line(&format!("int64_t {tmp} = mako_now_ms();"));
                            return ("int64_t".into(), tmp);
                        }
                        "runtime_stats_json" => {
                            let tmp = self.fresh("rstats");
                            self.line(&format!("MakoString {tmp} = mako_runtime_stats_json();"));
                            return ("MakoString".into(), tmp);
                        }
                        "runtime_stats_reset" => {
                            self.line("mako_runtime_stats_reset();");
                            return ("void".into(), "/*void*/".into());
                        }
                        "now_ns" => {
                            let tmp = self.fresh("nns");
                            self.line(&format!("int64_t {tmp} = mako_now_ns();"));
                            return ("int64_t".into(), tmp);
                        }
                        "black_box" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("bb");
                            self.line(&format!("int64_t {tmp} = mako_black_box_i64({x});"));
                            return ("int64_t".into(), tmp);
                        }
                        "path_join" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("pj");
                            self.line(&format!("MakoString {tmp} = mako_path_join({a}, {b});"));
                            return ("MakoString".into(), tmp);
                        }
                        "path_clean" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pc");
                            self.line(&format!("MakoString {tmp} = mako_path_clean({p});"));
                            return ("MakoString".into(), tmp);
                        }
                        "path_base" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pb");
                            self.line(&format!("MakoString {tmp} = mako_path_base({p});"));
                            return ("MakoString".into(), tmp);
                        }
                        "path_dir" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pd");
                            self.line(&format!("MakoString {tmp} = mako_path_dir({p});"));
                            return ("MakoString".into(), tmp);
                        }
                        "path_ext" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pe");
                            self.line(&format!("MakoString {tmp} = mako_path_ext({p});"));
                            return ("MakoString".into(), tmp);
                        }
                        "path_is_abs" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("bool".into(), format!("(bool)mako_path_is_abs({p})"));
                        }
                        "getcwd" => {
                            let tmp = self.fresh("cwd");
                            self.line(&format!("MakoString {tmp} = mako_getcwd();"));
                            return ("MakoString".into(), tmp);
                        }
                        "chdir" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_chdir({p})"));
                        }
                        "read_dir" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rd");
                            self.line(&format!("MakoStrArray {tmp} = mako_read_dir({p});"));
                            return ("MakoStrArray".into(), tmp);
                        }
                        "is_dir" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("bool".into(), format!("(bool)mako_is_dir({p})"));
                        }
                        "abs" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_abs({x})"));
                        }
                        "min" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_min({a}, {b})"));
                        }
                        "max" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_max({a}, {b})"));
                        }
                        "clamp" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            let (_, lo) = self.emit_expr(&args[1]);
                            let (_, hi) = self.emit_expr(&args[2]);
                            return ("int64_t".into(), format!("mako_clamp({x}, {lo}, {hi})"));
                        }
                        "math_abs" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            return ("double".into(), format!("mako_math_abs({x})"));
                        }
                        "math_sqrt" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            return ("double".into(), format!("mako_math_sqrt({x})"));
                        }
                        "math_pow" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            let (_, y) = self.emit_expr(&args[1]);
                            return ("double".into(), format!("mako_math_pow({x}, {y})"));
                        }
                        "math_floor" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            return ("double".into(), format!("mako_math_floor({x})"));
                        }
                        "math_ceil" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            return ("double".into(), format!("mako_math_ceil({x})"));
                        }
                        "math_sin" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            return ("double".into(), format!("mako_math_sin({x})"));
                        }
                        "math_cos" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            return ("double".into(), format!("mako_math_cos({x})"));
                        }
                        "math_log" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            return ("double".into(), format!("mako_math_log({x})"));
                        }
                        "math_exp" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            return ("double".into(), format!("mako_math_exp({x})"));
                        }
                        "ints_contains" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            return ("bool".into(), format!("(bool)mako_ints_contains({a}, {v})"));
                        }
                        "strings_contains" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            return (
                                "bool".into(),
                                format!("(bool)mako_strings_contains({a}, {v})"),
                            );
                        }
                        "ints_copy" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ic");
                            self.line(&format!("MakoIntArray {tmp} = mako_ints_copy({a});"));
                            return ("MakoIntArray".into(), tmp);
                        }
                        "ints_index" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_ints_index({a}, {v})"));
                        }
                        "time_format" => {
                            let (_, ms) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tf");
                            self.line(&format!(
                                "MakoString {tmp} = mako_time_format_rfc3339({ms});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "time_unix" => {
                            let tmp = self.fresh("tu");
                            self.line(&format!("int64_t {tmp} = mako_time_unix();"));
                            return ("int64_t".into(), tmp);
                        }
                        "mutex_new" => {
                            let tmp = self.fresh("mx");
                            self.line(&format!("MakoMutex *{tmp} = mako_mutex_new();"));
                            return ("MakoMutex*".into(), tmp);
                        }
                        "mutex_lock" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_mutex_lock({m});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "mutex_unlock" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_mutex_unlock({m});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "rwmutex_new" => {
                            let tmp = self.fresh("rw");
                            self.line(&format!("MakoRWMutex *{tmp} = mako_rwmutex_new();"));
                            return ("MakoRWMutex*".into(), tmp);
                        }
                        "rwmutex_rlock" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_rwmutex_rlock({m});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "rwmutex_runlock" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_rwmutex_runlock({m});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "rwmutex_lock" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_rwmutex_lock({m});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "rwmutex_unlock" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_rwmutex_unlock({m});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "cmap_new" => {
                            let tmp = self.fresh("cm");
                            self.line(&format!("MakoCMap *{tmp} = mako_cmap_new();"));
                            return ("MakoCMap*".into(), tmp);
                        }
                        "cmap_set" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let (_, v) = self.emit_expr(&args[2]);
                            self.line(&format!("mako_cmap_set({m}, {k}, {v});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "cmap_get" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("cg");
                            self.line(&format!("MakoString {tmp} = mako_cmap_get({m}, {k});"));
                            return ("MakoString".into(), tmp);
                        }
                        "cmap_has" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ch");
                            self.line(&format!("int64_t {tmp} = mako_cmap_has({m}, {k});"));
                            return ("int64_t".into(), tmp);
                        }
                        "cmap_del" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("cd");
                            self.line(&format!("int64_t {tmp} = mako_cmap_del({m}, {k});"));
                            return ("int64_t".into(), tmp);
                        }
                        "cmap_len" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("cl");
                            self.line(&format!("int64_t {tmp} = mako_cmap_len({m});"));
                            return ("int64_t".into(), tmp);
                        }
                        "cmap_incr" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let (_, d) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("ci");
                            self.line(&format!("int64_t {tmp} = mako_cmap_incr({m}, {k}, {d});"));
                            return ("int64_t".into(), tmp);
                        }
                        // Direct I/O
                        "file_open" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            let (_, f) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("fo");
                            self.line(&format!("int64_t {tmp} = mako_file_open({p}, {m}, {f});"));
                            return ("int64_t".into(), tmp);
                        }
                        "file_close" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("fc");
                            self.line(&format!("int64_t {tmp} = mako_file_close({f});"));
                            return ("int64_t".into(), tmp);
                        }
                        "pread" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, c) = self.emit_expr(&args[1]);
                            let (_, o) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("pr");
                            self.line(&format!("MakoString {tmp} = mako_pread({f}, {c}, {o});"));
                            return ("MakoString".into(), tmp);
                        }
                        "pwrite" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            let (_, o) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("pw");
                            self.line(&format!("int64_t {tmp} = mako_pwrite({f}, {d}, {o});"));
                            return ("int64_t".into(), tmp);
                        }
                        "file_append" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("fa");
                            self.line(&format!("int64_t {tmp} = mako_file_append({f}, {d});"));
                            return ("int64_t".into(), tmp);
                        }
                        "fsync" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("fs");
                            self.line(&format!("int64_t {tmp} = mako_fsync({f});"));
                            return ("int64_t".into(), tmp);
                        }
                        "fdatasync" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("fds");
                            self.line(&format!("int64_t {tmp} = mako_fdatasync({f});"));
                            return ("int64_t".into(), tmp);
                        }
                        "fallocate" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("fal");
                            self.line(&format!("int64_t {tmp} = mako_fallocate({f}, {s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "file_size" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("fsz");
                            self.line(&format!("int64_t {tmp} = mako_file_size({f});"));
                            return ("int64_t".into(), tmp);
                        }
                        "file_truncate" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ft");
                            self.line(&format!("int64_t {tmp} = mako_file_truncate({f}, {s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "file_seek" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, o) = self.emit_expr(&args[1]);
                            let (_, w) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("fsk");
                            self.line(&format!("int64_t {tmp} = mako_file_seek({f}, {o}, {w});"));
                            return ("int64_t".into(), tmp);
                        }
                        "file_read_exact" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("fre");
                            self.line(&format!("MakoString {tmp} = mako_file_read_exact({f}, {n});"));
                            return ("MakoString".into(), tmp);
                        }
                        "mmap_open" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("mm");
                            self.line(&format!("MakoMMap *{tmp} = mako_mmap_open({p}, {m});"));
                            return ("MakoMMap*".into(), tmp);
                        }
                        "mmap_create" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("mc");
                            self.line(&format!("MakoMMap *{tmp} = mako_mmap_create({p}, {s});"));
                            return ("MakoMMap*".into(), tmp);
                        }
                        "mmap_read" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let (_, o) = self.emit_expr(&args[1]);
                            let (_, c) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("mr");
                            self.line(&format!("MakoString {tmp} = mako_mmap_read({m}, {o}, {c});"));
                            return ("MakoString".into(), tmp);
                        }
                        "mmap_write" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let (_, o) = self.emit_expr(&args[1]);
                            let (_, d) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("mw");
                            self.line(&format!("int64_t {tmp} = mako_mmap_write({m}, {o}, {d});"));
                            return ("int64_t".into(), tmp);
                        }
                        "mmap_sync" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let (_, f) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ms");
                            self.line(&format!("int64_t {tmp} = mako_mmap_sync({m}, {f});"));
                            return ("int64_t".into(), tmp);
                        }
                        "mmap_size" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("msz");
                            self.line(&format!("int64_t {tmp} = mako_mmap_size({m});"));
                            return ("int64_t".into(), tmp);
                        }
                        "mmap_close" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("mcl");
                            self.line(&format!("int64_t {tmp} = mako_mmap_close({m});"));
                            return ("int64_t".into(), tmp);
                        }
                        // Event loop
                        "evloop_new" => {
                            let tmp = self.fresh("el");
                            self.line(&format!("MakoEvLoop *{tmp} = mako_evloop_new();"));
                            return ("MakoEvLoop*".into(), tmp);
                        }
                        "evloop_add" => {
                            let (_, el) = self.emit_expr(&args[0]);
                            let (_, fd) = self.emit_expr(&args[1]);
                            let (_, fl) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("ea");
                            self.line(&format!("int64_t {tmp} = mako_evloop_add({el}, {fd}, {fl});"));
                            return ("int64_t".into(), tmp);
                        }
                        "evloop_mod" => {
                            let (_, el) = self.emit_expr(&args[0]);
                            let (_, fd) = self.emit_expr(&args[1]);
                            let (_, fl) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("em");
                            self.line(&format!("int64_t {tmp} = mako_evloop_mod({el}, {fd}, {fl});"));
                            return ("int64_t".into(), tmp);
                        }
                        "evloop_del" => {
                            let (_, el) = self.emit_expr(&args[0]);
                            let (_, fd) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ed");
                            self.line(&format!("int64_t {tmp} = mako_evloop_del({el}, {fd});"));
                            return ("int64_t".into(), tmp);
                        }
                        "evloop_wait" => {
                            let (_, el) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ew");
                            self.line(&format!("int64_t {tmp} = mako_evloop_wait({el}, {t});"));
                            return ("int64_t".into(), tmp);
                        }
                        "evloop_event_fd" => {
                            let (_, el) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ef");
                            self.line(&format!("int64_t {tmp} = mako_evloop_event_fd({el}, {i});"));
                            return ("int64_t".into(), tmp);
                        }
                        "evloop_event_flags" => {
                            let (_, el) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("efl");
                            self.line(&format!("int64_t {tmp} = mako_evloop_event_flags({el}, {i});"));
                            return ("int64_t".into(), tmp);
                        }
                        "evloop_close" => {
                            let (_, el) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ecl");
                            self.line(&format!("int64_t {tmp} = mako_evloop_close({el});"));
                            return ("int64_t".into(), tmp);
                        }
                        "evloop_shutdown" => {
                            let (_, el) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("evs");
                            self.line(&format!("int64_t {tmp} = mako_evloop_shutdown({el});"));
                            return ("int64_t".into(), tmp);
                        }
                        "crew_drain" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, ms) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("cdr");
                            self.line(&format!(
                                "int64_t {tmp} = mako_crew_drain(&{c}, {ms});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "nb_listen" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("nl");
                            self.line(&format!("int64_t {tmp} = mako_nb_listen({p});"));
                            return ("int64_t".into(), tmp);
                        }
                        "nb_accept" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("na");
                            self.line(&format!("int64_t {tmp} = mako_nb_accept({f});"));
                            return ("int64_t".into(), tmp);
                        }
                        "nb_read" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("nr");
                            self.line(&format!("MakoString {tmp} = mako_nb_read({f});"));
                            return ("MakoString".into(), tmp);
                        }
                        "nb_write" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("nw");
                            self.line(&format!("int64_t {tmp} = mako_nb_write({f}, {d});"));
                            return ("int64_t".into(), tmp);
                        }
                        "nb_udp_bind" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("nub");
                            self.line(&format!("int64_t {tmp} = mako_nb_udp_bind({p});"));
                            return ("int64_t".into(), tmp);
                        }
                        "nb_udp_recv" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("nur");
                            self.line(&format!("MakoString {tmp} = mako_nb_udp_recv({f});"));
                            return ("MakoString".into(), tmp);
                        }
                        "nb_close" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("nc");
                            self.line(&format!("int64_t {tmp} = mako_nb_close({f});"));
                            return ("int64_t".into(), tmp);
                        }
                        // Binary buffer
                        "buf_pack_new" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("bp");
                            self.line(&format!("MakoBuf *{tmp} = mako_buf_new({c});"));
                            return ("MakoBuf*".into(), tmp);
                        }
                        "buf_from_string" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("bfs");
                            self.line(&format!("MakoBuf *{tmp} = mako_buf_from_string({s});"));
                            return ("MakoBuf*".into(), tmp);
                        }
                        "buf_to_string" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("bts");
                            self.line(&format!("MakoString {tmp} = mako_buf_to_string({b});"));
                            return ("MakoString".into(), tmp);
                        }
                        "buf_len" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("bl");
                            self.line(&format!("int64_t {tmp} = mako_buf_len({b});"));
                            return ("int64_t".into(), tmp);
                        }
                        "buf_pos" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("bpo");
                            self.line(&format!("int64_t {tmp} = mako_buf_pos({b});"));
                            return ("int64_t".into(), tmp);
                        }
                        "buf_reset" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_buf_reset({b});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "buf_seek" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_buf_seek({b}, {p});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "buf_free" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_buf_free({b});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "buf_write_u8" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_buf_write_u8({b}, {v});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "buf_write_u16" | "buf_write_u32" | "buf_write_u64" | "buf_write_i32" | "buf_write_u16be" | "buf_write_u32be" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_{name}({b}, {v});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "buf_write_f32" | "buf_write_f64" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_{name}({b}, {v});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "buf_write_bytes" | "buf_write_str" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_{name}({b}, {s});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "buf_read_u8" | "buf_read_u16" | "buf_read_u32" | "buf_read_u64" | "buf_read_i32" | "buf_read_u16be" | "buf_read_u32be" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("br");
                            self.line(&format!("int64_t {tmp} = mako_{name}({b});"));
                            return ("int64_t".into(), tmp);
                        }
                        "buf_read_f32" | "buf_read_f64" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("bf");
                            self.line(&format!("double {tmp} = mako_{name}({b});"));
                            return ("double".into(), tmp);
                        }
                        "buf_read_bytes" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("brb");
                            self.line(&format!("MakoString {tmp} = mako_buf_read_bytes({b}, {n});"));
                            return ("MakoString".into(), tmp);
                        }
                        "buf_read_str" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("brs");
                            self.line(&format!("MakoString {tmp} = mako_buf_read_str({b});"));
                            return ("MakoString".into(), tmp);
                        }
                        // Game UDP
                        "game_udp_bind" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gub");
                            // Bind all interfaces: wildcard host through the addr helper.
                            self.line(&format!(
                                "MakoGameUDP *{tmp} = mako_game_udp_bind_addr(mako_str_from_cstr(\"*\"), {p});"
                            ));
                            return ("MakoGameUDP*".into(), tmp);
                        }
                        "game_udp_bind_addr" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("guba");
                            self.line(&format!("MakoGameUDP *{tmp} = mako_game_udp_bind_addr({h}, {p});"));
                            return ("MakoGameUDP*".into(), tmp);
                        }
                        "game_udp_recv" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gur");
                            self.line(&format!("MakoString {tmp} = mako_game_udp_recv({u});"));
                            return ("MakoString".into(), tmp);
                        }
                        "game_udp_sender" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gus");
                            self.line(&format!("int64_t {tmp} = mako_game_udp_sender({u});"));
                            return ("int64_t".into(), tmp);
                        }
                        "game_udp_sender_addr" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gsa");
                            self.line(&format!(
                                "MakoString {tmp} = mako_game_udp_sender_addr({u});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "game_udp_send_to" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let (_, h) = self.emit_expr(&args[1]);
                            let (_, p) = self.emit_expr(&args[2]);
                            let (_, d) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("gst");
                            self.line(&format!(
                                "int64_t {tmp} = mako_game_udp_send_to({u}, {h}, {p}, {d});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "game_udp_send" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, d) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("gsd");
                            self.line(&format!("int64_t {tmp} = mako_game_udp_send({u}, {p}, {d});"));
                            return ("int64_t".into(), tmp);
                        }
                        "game_udp_broadcast" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("gbc");
                            self.line(&format!("int64_t {tmp} = mako_game_udp_broadcast({u}, {d});"));
                            return ("int64_t".into(), tmp);
                        }
                        "game_udp_kick" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_game_udp_kick({u}, {p});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "game_udp_peers" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gup");
                            self.line(&format!("int64_t {tmp} = mako_game_udp_peers({u});"));
                            return ("int64_t".into(), tmp);
                        }
                        "game_udp_fd" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("guf");
                            self.line(&format!("int64_t {tmp} = mako_game_udp_fd({u});"));
                            return ("int64_t".into(), tmp);
                        }
                        "game_udp_close" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_game_udp_close({u});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        // Tick timer
                        "tick_now_us" => {
                            let tmp = self.fresh("tnu");
                            self.line(&format!("int64_t {tmp} = mako_tick_now_us();"));
                            return ("int64_t".into(), tmp);
                        }
                        "tick_sleep_us" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tsu");
                            self.line(&format!("int64_t {tmp} = mako_tick_sleep_us({s}, {i});"));
                            return ("int64_t".into(), tmp);
                        }
                        // Cloud / distributed
                        "chash_new" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ch");
                            self.line(&format!("MakoCHash *{tmp} = mako_chash_new({n}, {v});"));
                            return ("MakoCHash*".into(), tmp);
                        }
                        "chash_get" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("cg");
                            self.line(&format!("int64_t {tmp} = mako_chash_get({r}, {k});"));
                            return ("int64_t".into(), tmp);
                        }
                        "chash_add_node" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ca");
                            self.line(&format!("int64_t {tmp} = mako_chash_add_node({r});"));
                            return ("int64_t".into(), tmp);
                        }
                        "chash_remove_node" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_chash_remove_node({r}, {n});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "chash_node_count" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("cn");
                            self.line(&format!("int64_t {tmp} = mako_chash_node_count({r});"));
                            return ("int64_t".into(), tmp);
                        }
                        "chash_free" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_chash_free({r});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "ratelimit_new" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("rl");
                            self.line(&format!("MakoRateLimiter *{tmp} = mako_ratelimit_new({r}, {b});"));
                            return ("MakoRateLimiter*".into(), tmp);
                        }
                        "ratelimit_allow" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ra");
                            self.line(&format!("int64_t {tmp} = mako_ratelimit_allow({r});"));
                            return ("int64_t".into(), tmp);
                        }
                        "ratelimit_remaining" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rr");
                            self.line(&format!("int64_t {tmp} = mako_ratelimit_remaining({r});"));
                            return ("int64_t".into(), tmp);
                        }
                        "ratelimit_free" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_ratelimit_free({r});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "breaker_new" => {
                            let (_, t) = self.emit_expr(&args[0]);
                            let (_, to) = self.emit_expr(&args[1]);
                            let (_, h) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("cb");
                            self.line(&format!("MakoCircuitBreaker *{tmp} = mako_breaker_new({t}, {to}, {h});"));
                            return ("MakoCircuitBreaker*".into(), tmp);
                        }
                        "breaker_allow" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ba");
                            self.line(&format!("int64_t {tmp} = mako_breaker_allow({c});"));
                            return ("int64_t".into(), tmp);
                        }
                        "breaker_success" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_breaker_success({c});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "breaker_failure" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_breaker_failure({c});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "breaker_state" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("bs");
                            self.line(&format!("int64_t {tmp} = mako_breaker_state({c});"));
                            return ("int64_t".into(), tmp);
                        }
                        "breaker_reset" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_breaker_reset({c});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "breaker_free" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_breaker_free({c});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "jwt_sign" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("js");
                            self.line(&format!("MakoString {tmp} = mako_jwt_sign({p}, {s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "jwt_verify" => {
                            let (_, t) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("jv");
                            self.line(&format!("int64_t {tmp} = mako_jwt_verify({t}, {s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "jwt_payload" => {
                            let (_, t) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("jp");
                            self.line(&format!("MakoString {tmp} = mako_jwt_payload({t});"));
                            return ("MakoString".into(), tmp);
                        }
                        "backoff_ms" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, m) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("bo");
                            self.line(&format!("int64_t {tmp} = mako_backoff_ms({a}, {b}, {m});"));
                            return ("int64_t".into(), tmp);
                        }
                        "env_get_or" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("eg");
                            self.line(&format!("MakoString {tmp} = mako_env_get_or({n}, {d});"));
                            return ("MakoString".into(), tmp);
                        }
                        "env_has" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("eh");
                            self.line(&format!("int64_t {tmp} = mako_env_has({n});"));
                            return ("int64_t".into(), tmp);
                        }
                        // HTTP Engine
                        "httpengine_new" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, w) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("he");
                            self.line(&format!("MakoHttpEngine *{tmp} = mako_httpengine_new({p}, {w});"));
                            return ("MakoHttpEngine*".into(), tmp);
                        }
                        "httpengine_route" => {
                            let (_, e) = self.emit_expr(&args[0]);
                            let (_, path) = self.emit_expr(&args[1]);
                            let (_, status) = self.emit_expr(&args[2]);
                            let (_, ctype) = self.emit_expr(&args[3]);
                            let (_, body) = self.emit_expr(&args[4]);
                            self.line(&format!("mako_httpengine_route({e}, {path}, {status}, {ctype}, {body});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "httpengine_serve" => {
                            let (_, e) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hs");
                            self.line(&format!("int64_t {tmp} = mako_httpengine_serve({e});"));
                            return ("int64_t".into(), tmp);
                        }
                        // Math
                        "sqrt" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("sq");
                            self.line(&format!("double {tmp} = mako_sqrt({x});"));
                            return ("double".into(), tmp);
                        }
                        "sin" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("sn");
                            self.line(&format!("double {tmp} = mako_sin({x});"));
                            return ("double".into(), tmp);
                        }
                        "cos" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("cs");
                            self.line(&format!("double {tmp} = mako_cos({x});"));
                            return ("double".into(), tmp);
                        }
                        "atan2" => {
                            let (_, y) = self.emit_expr(&args[0]);
                            let (_, x) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("at");
                            self.line(&format!("double {tmp} = mako_atan2({y}, {x});"));
                            return ("double".into(), tmp);
                        }
                        "floor_f" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("fl");
                            self.line(&format!("double {tmp} = mako_floor({x});"));
                            return ("double".into(), tmp);
                        }
                        "ceil_f" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ce");
                            self.line(&format!("double {tmp} = mako_ceil({x});"));
                            return ("double".into(), tmp);
                        }
                        "abs_f" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ab");
                            self.line(&format!("double {tmp} = mako_abs_f({x});"));
                            return ("double".into(), tmp);
                        }
                        "dist2d" => {
                            let (_, x1) = self.emit_expr(&args[0]);
                            let (_, y1) = self.emit_expr(&args[1]);
                            let (_, x2) = self.emit_expr(&args[2]);
                            let (_, y2) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("d2");
                            self.line(&format!("double {tmp} = mako_dist2d({x1}, {y1}, {x2}, {y2});"));
                            return ("double".into(), tmp);
                        }
                        "lerp" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, t) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("lp");
                            self.line(&format!("double {tmp} = mako_lerp({a}, {b}, {t});"));
                            return ("double".into(), tmp);
                        }
                        "clamp_f" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let (_, lo) = self.emit_expr(&args[1]);
                            let (_, hi) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("cl");
                            self.line(&format!("double {tmp} = mako_clamp_f({v}, {lo}, {hi});"));
                            return ("double".into(), tmp);
                        }
                        "random_bytes" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rb");
                            self.line(&format!("MakoString {tmp} = mako_random_bytes({n});"));
                            return ("MakoString".into(), tmp);
                        }
                        "random_int" => {
                            let (_, lo) = self.emit_expr(&args[0]);
                            let (_, hi) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_random_int({lo}, {hi})"));
                        }
                        "log_debug" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_log_debug({s});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "log_kv" => {
                            let (_, lvl) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let (_, v) = self.emit_expr(&args[2]);
                            self.line(&format!("mako_log_kv({lvl}, {k}, {v});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "read_file" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rf");
                            self.line(&format!("MakoString {tmp} = mako_read_file({p});"));
                            return ("MakoString".into(), tmp);
                        }
                        "write_file" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, c) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("wf");
                            self.line(&format!("int64_t {tmp} = mako_write_file({p}, {c});"));
                            return ("int64_t".into(), tmp);
                        }
                        "append_file" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, c) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("af");
                            self.line(&format!("int64_t {tmp} = mako_append_file({p}, {c});"));
                            return ("int64_t".into(), tmp);
                        }
                        "env_get" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("eg");
                            self.line(&format!("MakoString {tmp} = mako_env_get({k});"));
                            return ("MakoString".into(), tmp);
                        }
                        "env_set" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("es");
                            self.line(&format!("int64_t {tmp} = mako_env_set({k}, {v});"));
                            return ("int64_t".into(), tmp);
                        }
                        "mkdir" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("md");
                            self.line(&format!("int64_t {tmp} = mako_mkdir({p});"));
                            return ("int64_t".into(), tmp);
                        }
                        "file_exists" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("fe");
                            self.line(&format!("bool {tmp} = mako_file_exists({p});"));
                            return ("bool".into(), tmp);
                        }
                        "remove_file" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rm");
                            self.line(&format!("int64_t {tmp} = mako_remove_file({p});"));
                            return ("int64_t".into(), tmp);
                        }
                        "parse_int" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pi");
                            self.line(&format!("MakoResultInt {tmp} = mako_parse_int({s});"));
                            return ("MakoResultInt".into(), tmp);
                        }
                        "parse_float" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pf");
                            self.line(&format!("double {tmp} = mako_parse_float({s});"));
                            return ("double".into(), tmp);
                        }
                        "base64_encode" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("b64e");
                            self.line(&format!("MakoString {tmp} = mako_base64_encode({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "base64_decode" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("b64d");
                            self.line(&format!("MakoString {tmp} = mako_base64_decode({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "sort_ints" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("si");
                            self.line(&format!("MakoIntArray {tmp} = mako_sort_ints({a});"));
                            return ("MakoIntArray".into(), tmp);
                        }
                        "sort_strings" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ss");
                            self.line(&format!("MakoStrArray {tmp} = mako_sort_strings({a});"));
                            return ("MakoStrArray".into(), tmp);
                        }
                        "http_get" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hg");
                            self.line(&format!("MakoString {tmp} = mako_http_get({u});"));
                            return ("MakoString".into(), tmp);
                        }
                        "http_post" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("hp");
                            self.line(&format!("MakoString {tmp} = mako_http_post({u}, {b});"));
                            return ("MakoString".into(), tmp);
                        }
                        "http_request" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let (_, u) = self.emit_expr(&args[1]);
                            let (_, b) = self.emit_expr(&args[2]);
                            let (_, t) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("hrq");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_request({m}, {u}, {b}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_get_timeout" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("hgt");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_get_timeout({u}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_post_timeout" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, t) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("hpt");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_post_timeout({u}, {b}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_last_status" => {
                            let tmp = self.fresh("hls");
                            self.line(&format!("int64_t {tmp} = mako_http_last_status();"));
                            return ("int64_t".into(), tmp);
                        }
                        "http_last_header" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hlh");
                            self.line(&format!("MakoString {tmp} = mako_http_last_header({n});"));
                            return ("MakoString".into(), tmp);
                        }
                        "udp_bind" => {
                            let (_, port) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("udp");
                            self.line(&format!("int64_t {tmp} = mako_udp_bind({port});"));
                            return ("int64_t".into(), tmp);
                        }
                        "udp_send_to" => {
                            let (_, fd) = self.emit_expr(&args[0]);
                            let (_, host) = self.emit_expr(&args[1]);
                            let (_, port) = self.emit_expr(&args[2]);
                            let (_, data) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("udps");
                            self.line(&format!(
                                "int64_t {tmp} = mako_udp_send_to({fd}, {host}, {port}, {data});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "udp_recv" => {
                            let (_, fd) = self.emit_expr(&args[0]);
                            let (_, max) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("udpr");
                            self.line(&format!("MakoString {tmp} = mako_udp_recv({fd}, {max});"));
                            return ("MakoString".into(), tmp);
                        }
                        "udp_local_port" => {
                            let (_, fd) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_udp_local_port({fd})"));
                        }
                        "udp_close" => {
                            let (_, fd) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_udp_close({fd})"));
                        }
                        "unix_socket_pair" => {
                            let tmp = self.fresh("usp");
                            self.line(&format!("int64_t {tmp} = mako_unix_socket_pair();"));
                            return ("int64_t".into(), tmp);
                        }
                        "unix_socket_pair_peer" => {
                            return ("int64_t".into(), "mako_unix_socket_pair_peer()".into());
                        }
                        "unix_write" => {
                            let (_, fd) = self.emit_expr(&args[0]);
                            let (_, data) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("uxw");
                            self.line(&format!("int64_t {tmp} = mako_unix_write({fd}, {data});"));
                            return ("int64_t".into(), tmp);
                        }
                        "unix_read" => {
                            let (_, fd) = self.emit_expr(&args[0]);
                            let (_, max) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("uxr");
                            self.line(&format!("MakoString {tmp} = mako_unix_read({fd}, {max});"));
                            return ("MakoString".into(), tmp);
                        }
                        "unix_close" => {
                            let (_, fd) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_unix_close({fd})"));
                        }
                        "http2_detect" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2");
                            self.line(&format!("bool {tmp} = mako_http2_detect({s});"));
                            return ("bool".into(), tmp);
                        }
                        "http2_settings_len" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2s");
                            self.line(&format!("int64_t {tmp} = mako_http2_settings_len({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_empty_settings" => {
                            let tmp = self.fresh("h2e");
                            self.line(&format!("MakoString {tmp} = mako_http2_empty_settings();"));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_settings_max_concurrent" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2smc");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_settings_max_concurrent({n});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_settings_ack" => {
                            let tmp = self.fresh("h2a");
                            self.line(&format!("MakoString {tmp} = mako_http2_settings_ack();"));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_client_preface" => {
                            let tmp = self.fresh("h2cp");
                            self.line(&format!("MakoString {tmp} = mako_http2_client_preface();"));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_server_preface" => {
                            let tmp = self.fresh("h2sp");
                            self.line(&format!("MakoString {tmp} = mako_http2_server_preface();"));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_is_settings_ack" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2ia");
                            self.line(&format!("bool {tmp} = mako_http2_is_settings_ack({s});"));
                            return ("bool".into(), tmp);
                        }
                        "http2_conn_reset" => {
                            self.line("mako_http2_conn_reset();");
                            return ("int64_t".into(), "0".into());
                        }
                        "http2_conn_new" => {
                            let tmp = self.fresh("h2c");
                            self.line(&format!(
                                "MakoHttp2Conn *{tmp} = mako_http2_conn_new();"
                            ));
                            return ("MakoHttp2Conn*".into(), tmp);
                        }
                        "http2_conn_use" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2u");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_conn_use({c});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_conn_free" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_http2_conn_free({c});"));
                            return ("int64_t".into(), "0".into());
                        }
                        "http2_conn_recv" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2cr");
                            self.line(&format!("int64_t {tmp} = mako_http2_conn_recv({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_conn_send_settings" => {
                            self.line("mako_http2_conn_send_settings();");
                            return ("int64_t".into(), "0".into());
                        }
                        "http2_conn_send_settings_ack" => {
                            self.line("mako_http2_conn_send_settings_ack();");
                            return ("int64_t".into(), "0".into());
                        }
                        "http2_conn_settings_ack_needed" => {
                            let tmp = self.fresh("h2san");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_conn_settings_ack_needed();"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_conn_auto_settings_ack" => {
                            let tmp = self.fresh("h2asa");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_conn_auto_settings_ack();"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_conn_pump" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2pump");
                            self.line(&format!("MakoString {tmp} = mako_http2_conn_pump({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_conn_goaway_last" => {
                            let tmp = self.fresh("h2gl");
                            self.line(&format!("int64_t {tmp} = mako_http2_conn_goaway_last();"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_conn_max_concurrent" => {
                            let tmp = self.fresh("h2mc");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_conn_max_concurrent();"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_conn_active_streams" => {
                            let tmp = self.fresh("h2as");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_conn_active_streams();"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_conn_set_server" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2ss");
                            self.line(&format!("int64_t {tmp} = mako_http2_conn_set_server({v});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_conn_is_server" => {
                            let tmp = self.fresh("h2is");
                            self.line(&format!("int64_t {tmp} = mako_http2_conn_is_server();"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_conn_preface_received" => {
                            let tmp = self.fresh("h2pr");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_conn_preface_received();"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_conn_settings_exchanged" => {
                            let tmp = self.fresh("h2se");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_conn_settings_exchanged();"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_conn_closing" => {
                            let tmp = self.fresh("h2cl");
                            self.line(&format!("int64_t {tmp} = mako_http2_conn_closing();"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_conn_header_block" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2chb");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_conn_header_block({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_conn_header_stream" => {
                            let tmp = self.fresh("h2chs");
                            self.line(&format!("int64_t {tmp} = mako_http2_conn_header_stream();"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_conn_header_assembling" => {
                            let tmp = self.fresh("h2cha");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_conn_header_assembling();"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_conn_send_goaway" => {
                            self.line("mako_http2_conn_send_goaway();");
                            return ("int64_t".into(), "0".into());
                        }
                        "http2_window_of" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2wo");
                            self.line(&format!("int64_t {tmp} = mako_http2_window_of({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_window_conn" => {
                            let tmp = self.fresh("h2wc");
                            self.line(&format!("int64_t {tmp} = mako_http2_window_conn();"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_window_blocked" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2wb");
                            self.line(&format!("int64_t {tmp} = mako_http2_window_blocked({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_window_consume" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("h2wco");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_window_consume({s}, {n});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_window_increment" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("h2wi");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_window_increment({s}, {n});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_frame_type" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2t");
                            self.line(&format!("int64_t {tmp} = mako_http2_frame_type({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_frame_stream" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2st");
                            self.line(&format!("int64_t {tmp} = mako_http2_frame_stream({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_frame_len" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2ln");
                            self.line(&format!("int64_t {tmp} = mako_http2_frame_len({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_frame_flags" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2fl");
                            self.line(&format!("int64_t {tmp} = mako_http2_frame_flags({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "hpack_decode_stub" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hp");
                            self.line(&format!("int64_t {tmp} = mako_hpack_decode_stub({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "hpack_encode_indexed" => {
                            let (_, i) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hpe");
                            self.line(&format!(
                                "MakoString {tmp} = mako_hpack_encode_indexed({i});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "hpack_decode_indexed" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hpd");
                            self.line(&format!("int64_t {tmp} = mako_hpack_decode_indexed({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "hpack_static_name" => {
                            let (_, i) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hps");
                            self.line(&format!("MakoString {tmp} = mako_hpack_static_name({i});"));
                            return ("MakoString".into(), tmp);
                        }
                        "hpack_static_value" => {
                            let (_, i) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hpv");
                            self.line(&format!("MakoString {tmp} = mako_hpack_static_value({i});"));
                            return ("MakoString".into(), tmp);
                        }
                        "hpack_encode_literal" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("hpl");
                            self.line(&format!(
                                "MakoString {tmp} = mako_hpack_encode_literal({n}, {v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "hpack_literal_name" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hpln");
                            self.line(&format!("MakoString {tmp} = mako_hpack_literal_name({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "hpack_literal_value" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hplv");
                            self.line(&format!(
                                "MakoString {tmp} = mako_hpack_literal_value({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "hpack_decode_block" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hpdb");
                            self.line(&format!("int64_t {tmp} = mako_hpack_decode_block({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "hpack_decoded_count" => {
                            let tmp = self.fresh("hpdc");
                            self.line(&format!("int64_t {tmp} = mako_hpack_decoded_count();"));
                            return ("int64_t".into(), tmp);
                        }
                        "hpack_decoded_name" => {
                            let (_, i) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hpdn");
                            self.line(&format!("MakoString {tmp} = mako_hpack_decoded_name({i});"));
                            return ("MakoString".into(), tmp);
                        }
                        "hpack_decoded_value" => {
                            let (_, i) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hpdv");
                            self.line(&format!(
                                "MakoString {tmp} = mako_hpack_decoded_value({i});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "hpack_decode_clear" => {
                            self.line("mako_hpack_decode_clear();");
                            return ("int64_t".into(), "0".into());
                        }
                        "http2_headers_frame" => {
                            let (_, st) = self.emit_expr(&args[0]);
                            let (_, bl) = self.emit_expr(&args[1]);
                            let (_, fl) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("h2h");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_headers_frame({st}, {bl}, {fl});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_data_frame" => {
                            let (_, st) = self.emit_expr(&args[0]);
                            let (_, pl) = self.emit_expr(&args[1]);
                            let (_, fl) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("h2d");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_data_frame({st}, {pl}, {fl});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_response" => {
                            let (_, st) = self.emit_expr(&args[0]);
                            let (_, code) = self.emit_expr(&args[1]);
                            let (_, body) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("h2rsp");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_response({st}, {code}, {body});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_ready_streams" => {
                            return ("int64_t".into(), "mako_http2_ready_streams()".into());
                        }
                        "http2_next_ready_stream" => {
                            return ("int64_t".into(), "mako_http2_next_ready_stream()".into());
                        }
                        "http2_stream_take" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_http2_stream_take({s})"));
                        }
                        "http2_stream_body" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2body");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_stream_body({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_stream_body_len" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_http2_stream_body_len_of({s})"),
                            );
                        }
                        "http2_stream_body_done" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_http2_stream_body_done({s})"),
                            );
                        }
                        "http2_frame_payload" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2p");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_frame_payload({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_continuation_frame" => {
                            let (_, st) = self.emit_expr(&args[0]);
                            let (_, bl) = self.emit_expr(&args[1]);
                            let (_, fl) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("h2c");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_continuation_frame({st}, {bl}, {fl});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_goaway_frame" => {
                            let (_, ls) = self.emit_expr(&args[0]);
                            let (_, ec) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("h2g");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_goaway_frame({ls}, {ec});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_ping_frame" => {
                            let (_, op) = self.emit_expr(&args[0]);
                            let (_, fl) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("h2pi");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_ping_frame({op}, {fl});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_window_update_frame" => {
                            let (_, st) = self.emit_expr(&args[0]);
                            let (_, inc) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("h2w");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_window_update_frame({st}, {inc});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_is_goaway" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2ig");
                            self.line(&format!("bool {tmp} = mako_http2_is_goaway({s});"));
                            return ("bool".into(), tmp);
                        }
                        "http2_is_ping" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2ip");
                            self.line(&format!("bool {tmp} = mako_http2_is_ping({s});"));
                            return ("bool".into(), tmp);
                        }
                        "http2_is_window_update" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2iw");
                            self.line(&format!("bool {tmp} = mako_http2_is_window_update({s});"));
                            return ("bool".into(), tmp);
                        }
                        "http2_goaway_last_stream" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2gl");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_goaway_last_stream({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_goaway_error" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2ge");
                            self.line(&format!("int64_t {tmp} = mako_http2_goaway_error({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_window_update_increment" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2wi");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_window_update_increment({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_rst_stream_frame" => {
                            let (_, st) = self.emit_expr(&args[0]);
                            let (_, ec) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("h2rs");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_rst_stream_frame({st}, {ec});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_priority_frame" => {
                            let (_, st) = self.emit_expr(&args[0]);
                            let (_, dep) = self.emit_expr(&args[1]);
                            let (_, w) = self.emit_expr(&args[2]);
                            let (_, ex) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("h2pr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_priority_frame({st}, {dep}, {w}, {ex});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_is_rst_stream" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2ir");
                            self.line(&format!("bool {tmp} = mako_http2_is_rst_stream({s});"));
                            return ("bool".into(), tmp);
                        }
                        "http2_is_priority" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2ipri");
                            self.line(&format!("bool {tmp} = mako_http2_is_priority({s});"));
                            return ("bool".into(), tmp);
                        }
                        "http2_rst_stream_error" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2re");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_rst_stream_error({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_priority_dep" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2pd");
                            self.line(&format!("int64_t {tmp} = mako_http2_priority_dep({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_priority_weight" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2pw");
                            self.line(&format!("int64_t {tmp} = mako_http2_priority_weight({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_priority_exclusive" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2pe");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_priority_exclusive({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_priority_apply" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2pa");
                            self.line(&format!("int64_t {tmp} = mako_http2_priority_apply({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_stream_priority_dep" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2spd");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_stream_priority_dep({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_stream_priority_weight" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2spw");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_stream_priority_weight({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_stream_priority_exclusive" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2spe");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_stream_priority_exclusive({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_stream_priority_child_count" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2spc");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_stream_priority_child_count({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_schedule_next" => {
                            let tmp = self.fresh("h2sn");
                            self.line(&format!("int64_t {tmp} = mako_http2_schedule_next();"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_push_promise_frame" => {
                            let (_, st) = self.emit_expr(&args[0]);
                            let (_, ps) = self.emit_expr(&args[1]);
                            let (_, bl) = self.emit_expr(&args[2]);
                            let (_, fl) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("h2pp");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_push_promise_frame({st}, {ps}, {bl}, {fl});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_is_push_promise" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2ipp");
                            self.line(&format!("bool {tmp} = mako_http2_is_push_promise({s});"));
                            return ("bool".into(), tmp);
                        }
                        "http2_push_promise_stream" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2pps");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_push_promise_stream({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_concat_frames" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("h2x");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_concat_frames({a}, {b});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_header_block" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, st) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("h2hb");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_header_block({s}, {st});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_frame_at" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("h2a");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http2_frame_at({s}, {i});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "hpack_dyn_insert" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("hdi");
                            self.line(&format!("int64_t {tmp} = mako_hpack_dyn_insert({n}, {v});"));
                            return ("int64_t".into(), tmp);
                        }
                        "hpack_dyn_name" => {
                            let tmp = self.fresh("hdn");
                            self.line(&format!("MakoString {tmp} = mako_hpack_dyn_name();"));
                            return ("MakoString".into(), tmp);
                        }
                        "hpack_dyn_value" => {
                            let tmp = self.fresh("hdv");
                            self.line(&format!("MakoString {tmp} = mako_hpack_dyn_value();"));
                            return ("MakoString".into(), tmp);
                        }
                        "hpack_dyn_clear" => {
                            self.line("mako_hpack_dyn_clear();");
                            return ("int64_t".into(), "0".into());
                        }
                        "hpack_dyn_len" => {
                            let tmp = self.fresh("hdl");
                            self.line(&format!("int64_t {tmp} = mako_hpack_dyn_len();"));
                            return ("int64_t".into(), tmp);
                        }
                        "hpack_dyn_name_at" => {
                            let (_, i) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hdna");
                            self.line(&format!("MakoString {tmp} = mako_hpack_dyn_name_at({i});"));
                            return ("MakoString".into(), tmp);
                        }
                        "hpack_dyn_value_at" => {
                            let (_, i) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hdva");
                            self.line(&format!("MakoString {tmp} = mako_hpack_dyn_value_at({i});"));
                            return ("MakoString".into(), tmp);
                        }
                        "hpack_huffman_encode" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hhe");
                            self.line(&format!(
                                "MakoString {tmp} = mako_hpack_huffman_encode({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "hpack_huffman_decode" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hhd");
                            self.line(&format!(
                                "MakoString {tmp} = mako_hpack_huffman_decode({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http2_stream_reset" => {
                            self.line("mako_http2_stream_reset();");
                            return ("int64_t".into(), "0".into());
                        }
                        "http2_stream_id" => {
                            let tmp = self.fresh("h2sid");
                            self.line(&format!("int64_t {tmp} = mako_http2_stream_id();"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_stream_state" => {
                            let tmp = self.fresh("h2ss");
                            self.line(&format!("int64_t {tmp} = mako_http2_stream_state();"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_stream_state_of" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2so");
                            self.line(&format!("int64_t {tmp} = mako_http2_stream_state_of({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_stream_apply" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2sa");
                            self.line(&format!("int64_t {tmp} = mako_http2_stream_apply({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_stream_apply_local" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2sal");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_stream_apply_local({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_stream_half_closed_remote" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2hcr");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_stream_half_closed_remote({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http2_stream_half_closed_local" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("h2hcl");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http2_stream_half_closed_local({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_detect" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qd");
                            self.line(&format!("bool {tmp} = mako_quic_detect({s});"));
                            return ("bool".into(), tmp);
                        }
                        "quic_long_header" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qlh");
                            self.line(&format!("bool {tmp} = mako_quic_long_header({s});"));
                            return ("bool".into(), tmp);
                        }
                        "quic_short_header" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qsh");
                            self.line(&format!("bool {tmp} = mako_quic_short_header({s});"));
                            return ("bool".into(), tmp);
                        }
                        "quic_spin_bit" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qsb");
                            self.line(&format!("int64_t {tmp} = mako_quic_spin_bit({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_key_phase" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qkp");
                            self.line(&format!("int64_t {tmp} = mako_quic_key_phase({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_long_type" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qlt");
                            self.line(&format!("int64_t {tmp} = mako_quic_long_type({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_is_retry" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qir");
                            self.line(&format!("bool {tmp} = mako_quic_is_retry({s});"));
                            return ("bool".into(), tmp);
                        }
                        "quic_is_version_negotiation" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qivn");
                            self.line(&format!(
                                "bool {tmp} = mako_quic_is_version_negotiation({s});"
                            ));
                            return ("bool".into(), tmp);
                        }
                        "quic_vn_version_count" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qvnc");
                            self.line(&format!("int64_t {tmp} = mako_quic_vn_version_count({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_vn_version_at" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("qvna");
                            self.line(&format!(
                                "int64_t {tmp} = mako_quic_vn_version_at({s}, {i});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_vn_select" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("qvns");
                            self.line(&format!("int64_t {tmp} = mako_quic_vn_select({s}, {p});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_has_crypto" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qhc");
                            self.line(&format!("bool {tmp} = mako_quic_has_crypto({s});"));
                            return ("bool".into(), tmp);
                        }
                        "quic_crypto_offset" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qco");
                            self.line(&format!("int64_t {tmp} = mako_quic_crypto_offset({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_crypto_data_offset" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qcdo");
                            self.line(&format!(
                                "int64_t {tmp} = mako_quic_crypto_data_offset({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_crypto_data_len" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qcdl");
                            self.line(&format!("int64_t {tmp} = mako_quic_crypto_data_len({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_crypto_data" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qcd");
                            self.line(&format!("MakoString {tmp} = mako_quic_crypto_data({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_crypto_frame" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, o) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("qcf");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_crypto_frame({d}, {o});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_crypto_payload" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, o) = self.emit_expr(&args[1]);
                            let (_, m) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("qcp");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_crypto_payload({d}, {o}, {m});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_payload_crypto_data" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qpcd");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_payload_crypto_data({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_payload_crypto_data_len" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qpcl");
                            self.line(&format!(
                                "int64_t {tmp} = mako_quic_payload_crypto_data_len({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_ack_frame" => {
                            let (_, l) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            let (_, f) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("qaf");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_ack_frame({l}, {d}, {f});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_ack_largest" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qal");
                            self.line(&format!("int64_t {tmp} = mako_quic_ack_largest({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_ack_delay" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qad");
                            self.line(&format!("int64_t {tmp} = mako_quic_ack_delay({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_ack_range_count" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qarc");
                            self.line(&format!("int64_t {tmp} = mako_quic_ack_range_count({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_ack_first_range" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qafr");
                            self.line(&format!("int64_t {tmp} = mako_quic_ack_first_range({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_ack_smallest" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qas");
                            self.line(&format!("int64_t {tmp} = mako_quic_ack_smallest({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_is_ack" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qia");
                            self.line(&format!("int64_t {tmp} = mako_quic_is_ack({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_stream_frame" => {
                            let (_, sid) = self.emit_expr(&args[0]);
                            let (_, off) = self.emit_expr(&args[1]);
                            let (_, data) = self.emit_expr(&args[2]);
                            let (_, fin) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("qsf");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_stream_frame({sid}, {off}, {data}, {fin});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_is_stream" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qis");
                            self.line(&format!("int64_t {tmp} = mako_quic_is_stream({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_stream_fin" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qsfin");
                            self.line(&format!("int64_t {tmp} = mako_quic_stream_fin({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_stream_id_of" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qsid");
                            self.line(&format!("int64_t {tmp} = mako_quic_stream_id_of({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_stream_offset" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qsoff");
                            self.line(&format!("int64_t {tmp} = mako_quic_stream_offset({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_stream_data_len" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qsdl");
                            self.line(&format!("int64_t {tmp} = mako_quic_stream_data_len({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_stream_data" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qsdata");
                            self.line(&format!("MakoString {tmp} = mako_quic_stream_data({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_version" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qv");
                            self.line(&format!("int64_t {tmp} = mako_quic_version({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_dcid_len" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qdl");
                            self.line(&format!("int64_t {tmp} = mako_quic_dcid_len({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_dcid" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qdc");
                            self.line(&format!("MakoString {tmp} = mako_quic_dcid({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_scid_len" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qsl");
                            self.line(&format!("int64_t {tmp} = mako_quic_scid_len({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_scid" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qsc");
                            self.line(&format!("MakoString {tmp} = mako_quic_scid({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_payload_offset" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qpo");
                            self.line(&format!("int64_t {tmp} = mako_quic_payload_offset({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "quic_hkdf_expand_label" => {
                            let (_, sec) = self.emit_expr(&args[0]);
                            let (_, lab) = self.emit_expr(&args[1]);
                            let (_, n) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("qhk");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_hkdf_expand_label({sec}, {lab}, {n});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_hkdf_expand_label_hex" => {
                            let (_, sec) = self.emit_expr(&args[0]);
                            let (_, lab) = self.emit_expr(&args[1]);
                            let (_, n) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("qhkh");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_hkdf_expand_label_hex({sec}, {lab}, {n});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_initial_client_secret" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qics");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_initial_client_secret({d});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_initial_client_secret_hex" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qicsh");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_initial_client_secret_hex({d});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_initial_client_key" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qick");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_initial_client_key({d});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_initial_client_iv" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qici");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_initial_client_iv({d});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_initial_client_key_hex" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qickh");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_initial_client_key_hex({d});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_initial_client_iv_hex" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qicih");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_initial_client_iv_hex({d});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_initial_client_hp" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qichp");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_initial_client_hp({d});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_initial_client_hp_hex" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qichph");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_initial_client_hp_hex({d});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_initial_protect" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, pn) = self.emit_expr(&args[1]);
                            let (_, a) = self.emit_expr(&args[2]);
                            let (_, p) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("qip");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_initial_protect({d}, {pn}, {a}, {p});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_initial_unprotect" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, pn) = self.emit_expr(&args[1]);
                            let (_, a) = self.emit_expr(&args[2]);
                            let (_, s) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("qiu");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_initial_unprotect({d}, {pn}, {a}, {s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_header_protection_mask" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("qhpm");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_header_protection_mask({k}, {s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_header_protection_mask_hex" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("qhpmh");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_header_protection_mask_hex({k}, {s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_initial_hp_mask" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("qihm");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_initial_hp_mask({d}, {s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_initial_hp_mask_hex" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("qihmh");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_initial_hp_mask_hex({d}, {s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_header_protect_apply" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, o) = self.emit_expr(&args[1]);
                            let (_, m) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("qhpa");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_header_protect_apply({h}, {o}, {m});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_header_protect_remove" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, o) = self.emit_expr(&args[1]);
                            let (_, m) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("qhpr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_header_protect_remove({h}, {o}, {m});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_initial_packet_protect" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, pn) = self.emit_expr(&args[1]);
                            let (_, h) = self.emit_expr(&args[2]);
                            let (_, o) = self.emit_expr(&args[3]);
                            let (_, p) = self.emit_expr(&args[4]);
                            let tmp = self.fresh("qipp");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_initial_packet_protect({d}, {pn}, {h}, {o}, {p});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quic_initial_packet_unprotect" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, pkt) = self.emit_expr(&args[1]);
                            let (_, hl) = self.emit_expr(&args[2]);
                            let (_, o) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("qipu");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quic_initial_packet_unprotect({d}, {pkt}, {hl}, {o});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "hex_decode" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hx");
                            self.line(&format!("MakoString {tmp} = mako_hex_decode({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_record_type" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("trt");
                            self.line(&format!("int64_t {tmp} = mako_tls_record_type({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_record_version" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("trv");
                            self.line(&format!("int64_t {tmp} = mako_tls_record_version({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_record_len" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("trl");
                            self.line(&format!("int64_t {tmp} = mako_tls_record_len({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_aead_seal" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, p) = self.emit_expr(&args[2]);
                            let (_, a) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("tas");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_aead_seal({k}, {n}, {p}, {a});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_aead_open" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, s) = self.emit_expr(&args[2]);
                            let (_, a) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("tao");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_aead_open({k}, {n}, {s}, {a});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_record_appdata_seal" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, p) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("tras");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_record_appdata_seal({k}, {n}, {p});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_record_appdata_open" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, r) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("trao");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_record_appdata_open({k}, {n}, {r});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_record_seq_reset" => {
                            let tmp = self.fresh("tsr");
                            self.line(&format!("int64_t {tmp} = mako_tls_record_seq_reset();"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_record_write_seq" => {
                            let tmp = self.fresh("tws");
                            self.line(&format!("int64_t {tmp} = mako_tls_record_write_seq();"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_record_read_seq" => {
                            let tmp = self.fresh("trs");
                            self.line(&format!("int64_t {tmp} = mako_tls_record_read_seq();"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_record_appdata_seal_seq" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, iv) = self.emit_expr(&args[1]);
                            let (_, p) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("trss");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_record_appdata_seal_seq({k}, {iv}, {p});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_record_appdata_open_seq" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, iv) = self.emit_expr(&args[1]);
                            let (_, r) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("tros");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_record_appdata_open_seq({k}, {iv}, {r});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_client_hello" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tch");
                            self.line(&format!("MakoString {tmp} = mako_tls_client_hello({r});"));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_client_hello_legacy_version" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tclv");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_client_hello_legacy_version({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_client_hello_random" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tchr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_client_hello_random({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_client_hello_has_aes128_gcm" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tcha");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_client_hello_has_aes128_gcm({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_server_hello" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tsh");
                            self.line(&format!("MakoString {tmp} = mako_tls_server_hello({r});"));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_server_hello_random" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tshr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_server_hello_random({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_certificate" => {
                            let (_, der) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tcert");
                            self.line(&format!("MakoString {tmp} = mako_tls_certificate({der});"));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_certificate_der" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tcder");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_certificate_der({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_certificate_verify" => {
                            let (_, sch) = self.emit_expr(&args[0]);
                            let (_, sig) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tcv");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_certificate_verify({sch}, {sig});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_certificate_verify_scheme" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tcvs");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_certificate_verify_scheme({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_certificate_verify_sig" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tcvg");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_certificate_verify_sig({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_encrypted_extensions" => {
                            let tmp = self.fresh("tee");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_encrypted_extensions();"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_finished" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tfin");
                            self.line(&format!("MakoString {tmp} = mako_tls_finished({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_hs_msg_type" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("thmt");
                            self.line(&format!("int64_t {tmp} = mako_tls_hs_msg_type({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_hs_reset" => {
                            let tmp = self.fresh("thsr");
                            self.line(&format!("int64_t {tmp} = mako_tls_hs_reset();"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_hs_state" => {
                            let tmp = self.fresh("thss");
                            self.line(&format!("int64_t {tmp} = mako_tls_hs_state_get();"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_hs_advance" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("thsa");
                            self.line(&format!("int64_t {tmp} = mako_tls_hs_advance({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_hs_is_app" => {
                            let tmp = self.fresh("thia");
                            self.line(&format!("int64_t {tmp} = mako_tls_hs_is_app();"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_hs_session_reset" => {
                            let tmp = self.fresh("thsr2");
                            self.line(&format!("int64_t {tmp} = mako_tls_hs_session_reset();"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_hs_session_feed" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("thsf");
                            self.line(&format!("int64_t {tmp} = mako_tls_hs_session_feed({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_hs_session_client_hello" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("thsch");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_hs_session_client_hello({r});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_hs_session_server_hello" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("thssh");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_hs_session_server_hello({r});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_hs_session_finished_hex" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("thsfh");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_hs_session_finished_hex({k});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_hs_session_encrypted_extensions" => {
                            let tmp = self.fresh("thsee");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_hs_session_encrypted_extensions();"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_hs_session_certificate" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("thsc");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_hs_session_certificate({d});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_hs_session_certificate_verify" => {
                            let (_, sch) = self.emit_expr(&args[0]);
                            let (_, sig) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("thscv");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_hs_session_certificate_verify({sch}, {sig});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_hs_session_finished" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("thsfin");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_hs_session_finished({k}, {v});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_finished_verify_data" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tfvd");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_finished_verify_data({k}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_finished_verify_data_hex" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tfvh");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_finished_verify_data_hex({k}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_transcript_reset" => {
                            let tmp = self.fresh("ttr");
                            self.line(&format!("int64_t {tmp} = mako_tls_transcript_reset();"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_transcript_append" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tta");
                            self.line(&format!("int64_t {tmp} = mako_tls_transcript_append({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_transcript_len" => {
                            let tmp = self.fresh("ttl");
                            self.line(&format!("int64_t {tmp} = mako_tls_transcript_len();"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_transcript_finished_hex" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ttf");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_transcript_finished_hex({k});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_derive_secret" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, l) = self.emit_expr(&args[1]);
                            let (_, t) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("tds");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_derive_secret({s}, {l}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_derive_secret_hex" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, l) = self.emit_expr(&args[1]);
                            let (_, t) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("tdsh");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_derive_secret_hex({s}, {l}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_client_handshake_traffic_secret" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tchts");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_client_handshake_traffic_secret({s}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_server_handshake_traffic_secret" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tshts");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_server_handshake_traffic_secret({s}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_client_handshake_traffic_secret_hex" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tchtsh");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_client_handshake_traffic_secret_hex({s}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_server_handshake_traffic_secret_hex" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tshtsh");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_server_handshake_traffic_secret_hex({s}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_client_application_traffic_secret" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tcats");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_client_application_traffic_secret({s}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_server_application_traffic_secret" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tsats");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_server_application_traffic_secret({s}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_client_application_traffic_secret_hex" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tcatsh");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_client_application_traffic_secret_hex({s}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_server_application_traffic_secret_hex" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tsatsh");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_server_application_traffic_secret_hex({s}, {t});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "pb_encode_varint" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pb");
                            self.line(&format!("MakoString {tmp} = mako_pb_encode_varint({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "pb_decode_varint" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pbd");
                            self.line(&format!("int64_t {tmp} = mako_pb_decode_varint({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "pb_varint_len" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pbl");
                            self.line(&format!("int64_t {tmp} = mako_pb_varint_len({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "pb_encode_key" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, w) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("pbk");
                            self.line(&format!("MakoString {tmp} = mako_pb_encode_key({f}, {w});"));
                            return ("MakoString".into(), tmp);
                        }
                        "pb_key_field" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pbkf");
                            self.line(&format!("int64_t {tmp} = mako_pb_key_field({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "pb_key_wire" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pbkw");
                            self.line(&format!("int64_t {tmp} = mako_pb_key_wire({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "pb_zigzag_encode" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pbz");
                            self.line(&format!("int64_t {tmp} = mako_pb_zigzag_encode({v});"));
                            return ("int64_t".into(), tmp);
                        }
                        "pb_zigzag_decode" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pbzd");
                            self.line(&format!("int64_t {tmp} = mako_pb_zigzag_decode({v});"));
                            return ("int64_t".into(), tmp);
                        }
                        "pb_encode_sint" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pbs");
                            self.line(&format!("MakoString {tmp} = mako_pb_encode_sint({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "pb_decode_sint" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pbsd");
                            self.line(&format!("int64_t {tmp} = mako_pb_decode_sint({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "pb_encode_bytes" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pbb");
                            self.line(&format!("MakoString {tmp} = mako_pb_encode_bytes({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "pb_bytes_len" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pbbl");
                            self.line(&format!("int64_t {tmp} = mako_pb_bytes_len({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "pb_encode_field_varint" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("pbfv");
                            self.line(&format!(
                                "MakoString {tmp} = mako_pb_encode_field_varint({f}, {v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "pb_encode_simple" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("pbsm");
                            self.line(&format!(
                                "MakoString {tmp} = mako_pb_encode_simple({n}, {i});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "pb_simple_name" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pbsn");
                            self.line(&format!("MakoString {tmp} = mako_pb_simple_name({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "pb_simple_id" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pbsi");
                            self.line(&format!("int64_t {tmp} = mako_pb_simple_id({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "pb_encode_nested" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            let (_, iname) = self.emit_expr(&args[2]);
                            let (_, iid) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("pbn");
                            self.line(&format!(
                                "MakoString {tmp} = mako_pb_encode_nested({n}, {i}, {iname}, {iid});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "pb_nested_inner" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pbni");
                            self.line(&format!("MakoString {tmp} = mako_pb_nested_inner({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "pb_encode_repeated_varint" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, c) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("pbr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_pb_encode_repeated_varint({a}, {b}, {c});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "pb_repeated_count" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, f) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("pbrc");
                            self.line(&format!(
                                "int64_t {tmp} = mako_pb_repeated_count({s}, {f});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "pb_repeated_at" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, f) = self.emit_expr(&args[1]);
                            let (_, ix) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("pbra");
                            self.line(&format!(
                                "int64_t {tmp} = mako_pb_repeated_at({s}, {f}, {ix});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "regex_match" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("rm");
                            self.line(&format!("bool {tmp} = mako_regex_match({p}, {t});"));
                            return ("bool".into(), tmp);
                        }
                        "regex_find" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("rf");
                            self.line(&format!("MakoString {tmp} = mako_regex_find({p}, {t});"));
                            return ("MakoString".into(), tmp);
                        }
                        "regex_capture" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let (_, n) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("rc");
                            self.line(&format!(
                                "MakoString {tmp} = mako_regex_capture({p}, {t}, {n});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "argc" => {
                            let tmp = self.fresh("ac");
                            self.line(&format!("int64_t {tmp} = mako_argc();"));
                            return ("int64_t".into(), tmp);
                        }
                        "args" => {
                            let tmp = self.fresh("av");
                            self.line(&format!("MakoStrArray {tmp} = mako_args();"));
                            return ("MakoStrArray".into(), tmp);
                        }
                        "arg_get" => {
                            let (_, i) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ag");
                            self.line(&format!("MakoString {tmp} = mako_arg_get({i});"));
                            return ("MakoString".into(), tmp);
                        }
                        "json_array_push_string" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("jps");
                            self.line(&format!(
                                "MakoString {tmp} = mako_json_array_push_string({a}, {v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "json_object_from_map_ss" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("jom");
                            self.line(&format!(
                                "MakoString {tmp} = mako_json_object_from_map_ss({m});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "actor_spawn" => {
                            let (_, cap) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("act");
                            self.line(&format!("MakoActor *{tmp} = mako_actor_spawn({cap});"));
                            return ("MakoChan*".into(), tmp);
                        }
                        "actor_send" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("as");
                            self.line(&format!("bool {tmp} = mako_actor_send({a}, {m}) != 0;"));
                            return ("bool".into(), tmp);
                        }
                        "actor_recv" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ar");
                            self.line(&format!("int64_t {tmp} = mako_actor_recv({a});"));
                            return ("int64_t".into(), tmp);
                        }
                        "actor_stop" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_actor_stop({a});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "tcp_listen" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tl");
                            self.line(&format!("int64_t {tmp} = mako_tcp_listen({p});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tcp_listen_addr" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tla");
                            self.line(&format!("int64_t {tmp} = mako_tcp_listen_addr({h}, {p});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tcp_accept" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ta");
                            self.line(&format!("int64_t {tmp} = mako_tcp_accept({f});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tcp_close" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tc");
                            self.line(&format!("int64_t {tmp} = mako_tcp_close({f});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tcp_write" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tw");
                            self.line(&format!("int64_t {tmp} = mako_tcp_write({f}, {s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tcp_read_print" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tr");
                            self.line(&format!("int64_t {tmp} = mako_tcp_read_print({f});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tcp_read" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("trd");
                            self.line(&format!("MakoString {tmp} = mako_tcp_read({f});"));
                            return ("MakoString".into(), tmp);
                        }
                        "tcp_nodelay" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tnd");
                            self.line(&format!("int64_t {tmp} = mako_tcp_nodelay({f});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tcp_set_timeout" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, ms) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tst");
                            self.line(&format!("int64_t {tmp} = mako_tcp_set_timeout({f}, {ms});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tcp_keepalive" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, idle) = self.emit_expr(&args[1]);
                            let (_, itv) = self.emit_expr(&args[2]);
                            let (_, cnt) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("tka");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tcp_keepalive({f}, {idle}, {itv}, {cnt});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tcp_listen_backlog" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, bl) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("tlb");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tcp_listen_backlog({h}, {p}, {bl});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "json_object" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("jo");
                            self.line(&format!(
                                "MakoString {tmp} = mako_json_object_str({k}, {v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "json_si" => {
                            let (_, k1) = self.emit_expr(&args[0]);
                            let (_, v1) = self.emit_expr(&args[1]);
                            let (_, k2) = self.emit_expr(&args[2]);
                            let (_, v2) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("jsi");
                            self.line(&format!(
                                "MakoString {tmp} = mako_json_si({k1}, {v1}, {k2}, {v2});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "json_ss" => {
                            let (_, k1) = self.emit_expr(&args[0]);
                            let (_, v1) = self.emit_expr(&args[1]);
                            let (_, k2) = self.emit_expr(&args[2]);
                            let (_, v2) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("jss");
                            self.line(&format!(
                                "MakoString {tmp} = mako_json_ss({k1}, {v1}, {k2}, {v2});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "json_i" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ji");
                            self.line(&format!("MakoString {tmp} = mako_json_i({k}, {v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "json_has" => {
                            let (_, j) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let (_, e) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_json_has_string({j}, {k}, {e})"),
                            );
                        }
                        "json_get_string" => {
                            let (_, j) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("jgs");
                            self.line(&format!(
                                "MakoString {tmp} = mako_json_get_string({j}, {k});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "json_get_int" => {
                            let (_, j) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_json_get_int({j}, {k})"));
                        }
                        "json_nest" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("jn");
                            self.line(&format!("MakoString {tmp} = mako_json_nest({k}, {v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "json_get_object" => {
                            let (_, j) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("jgo");
                            self.line(&format!(
                                "MakoString {tmp} = mako_json_get_object({j}, {k});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "json_path_string" => {
                            let (_, j) = self.emit_expr(&args[0]);
                            let (_, k1) = self.emit_expr(&args[1]);
                            let (_, k2) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("jps");
                            self.line(&format!(
                                "MakoString {tmp} = mako_json_path_string({j}, {k1}, {k2});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "json_path_int" => {
                            let (_, j) = self.emit_expr(&args[0]);
                            let (_, k1) = self.emit_expr(&args[1]);
                            let (_, k2) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_json_path_int({j}, {k1}, {k2})"),
                            );
                        }
                        "json_merge" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("jm");
                            self.line(&format!("MakoString {tmp} = mako_json_merge({a}, {b});"));
                            return ("MakoString".into(), tmp);
                        }
                        "openapi_route" => {
                            let (_, method) = self.emit_expr(&args[0]);
                            let (_, path) = self.emit_expr(&args[1]);
                            let (_, summary) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("oar");
                            self.line(&format!(
                                "MakoString {tmp} = mako_openapi_route({method}, {path}, {summary});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "openapi_doc" => {
                            let (_, title) = self.emit_expr(&args[0]);
                            let (_, version) = self.emit_expr(&args[1]);
                            let (_, paths) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("oad");
                            self.line(&format!(
                                "MakoString {tmp} = mako_openapi_doc({title}, {version}, {paths});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "graphql_field" => {
                            let (_, query) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gqlf");
                            self.line(&format!("MakoString {tmp} = mako_graphql_field({query});"));
                            return ("MakoString".into(), tmp);
                        }
                        "graphql_arg" => {
                            let (_, query) = self.emit_expr(&args[0]);
                            let (_, name) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("gqla");
                            self.line(&format!(
                                "MakoString {tmp} = mako_graphql_arg({query}, {name});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "graphql_data" => {
                            let (_, field) = self.emit_expr(&args[0]);
                            let (_, json) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("gqld");
                            self.line(&format!(
                                "MakoString {tmp} = mako_graphql_data({field}, {json});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "graphql_error" => {
                            let (_, message) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gqle");
                            self.line(&format!(
                                "MakoString {tmp} = mako_graphql_error({message});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "graphql_request" => {
                            let (_, query) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gqlr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_graphql_request({query});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "graphql_is_mutation" => {
                            let (_, query) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_graphql_is_mutation({query})"),
                            );
                        }
                        "sse_event" => {
                            let (_, event) = self.emit_expr(&args[0]);
                            let (_, data) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("sse");
                            self.line(&format!(
                                "MakoString {tmp} = mako_sse_event({event}, {data});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "sse_retry" => {
                            let (_, millis) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("sser");
                            self.line(&format!("MakoString {tmp} = mako_sse_retry({millis});"));
                            return ("MakoString".into(), tmp);
                        }
                        "rpc_frame" => {
                            let (_, method) = self.emit_expr(&args[0]);
                            let (_, payload) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("rpcf");
                            self.line(&format!(
                                "MakoString {tmp} = mako_rpc_frame({method}, {payload});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "rpc_method" => {
                            let (_, frame) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rpcm");
                            self.line(&format!("MakoString {tmp} = mako_rpc_method({frame});"));
                            return ("MakoString".into(), tmp);
                        }
                        "rpc_payload" => {
                            let (_, frame) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rpcp");
                            self.line(&format!("MakoString {tmp} = mako_rpc_payload({frame});"));
                            return ("MakoString".into(), tmp);
                        }
                        "json_array_len" => {
                            let (_, j) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_json_array_len({j})"));
                        }
                        "json_array_get_int" => {
                            let (_, j) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_json_array_get_int({j}, {i})"),
                            );
                        }
                        "json_array_get_string" => {
                            let (_, j) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("jags");
                            self.line(&format!(
                                "MakoString {tmp} = mako_json_array_get_string({j}, {i});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "json_array_ints3" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, c) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("jai");
                            self.line(&format!(
                                "MakoString {tmp} = mako_json_array_ints3({a}, {b}, {c});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "json_array_strings2" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("jas");
                            self.line(&format!(
                                "MakoString {tmp} = mako_json_array_strings2({a}, {b});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "json_array_push_int" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("jap");
                            self.line(&format!(
                                "MakoString {tmp} = mako_json_array_push_int({a}, {v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "bin_encode_int" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("be");
                            self.line(&format!("MakoString {tmp} = mako_bin_encode_int({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "yaml_get_string" => {
                            let (_, doc) = self.emit_expr(&args[0]);
                            let (_, key) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ygs");
                            self.line(&format!(
                                "MakoString {tmp} = mako_yaml_get_string({doc}, {key});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "toml_get_string" => {
                            let (_, doc) = self.emit_expr(&args[0]);
                            let (_, key) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tgs");
                            self.line(&format!(
                                "MakoString {tmp} = mako_toml_get_string({doc}, {key});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "toml_get_int" => {
                            let (_, doc) = self.emit_expr(&args[0]);
                            let (_, key) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_toml_get_int({doc}, {key})"));
                        }
                        "msgpack_int_hex" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("mp");
                            self.line(&format!("MakoString {tmp} = mako_msgpack_int_hex({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "cbor_int_hex" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("cb");
                            self.line(&format!("MakoString {tmp} = mako_cbor_int_hex({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "avro_long_hex" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("av");
                            self.line(&format!("MakoString {tmp} = mako_avro_long_hex({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "sha256" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("sh");
                            self.line(&format!("MakoString {tmp} = mako_sha256_hex({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "hmac_sha256" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("hm");
                            self.line(&format!(
                                "MakoString {tmp} = mako_hmac_sha256_hex({k}, {m});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "sha256_raw" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("shr");
                            self.line(&format!("MakoString {tmp} = mako_sha256_raw({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "xor_bytes" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("xb");
                            self.line(&format!("MakoString {tmp} = mako_xor_bytes({a}, {b});"));
                            return ("MakoString".into(), tmp);
                        }
                        "hmac_sha256_raw" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("hmr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_hmac_sha256_raw({k}, {m});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "pbkdf2_sha256" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            let (_, it) = self.emit_expr(&args[2]);
                            let (_, dk) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("pbk");
                            self.line(&format!(
                                "MakoString {tmp} = mako_pbkdf2_sha256({p}, {s}, {it}, {dk});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "log_info" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_log_info({s});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "log_warn" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_log_warn({s});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "log_error" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_log_error({s});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "validate_required" => {
                            let (_, value) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_validate_required({value})"));
                        }
                        "validate_min_len" => {
                            let (_, value) = self.emit_expr(&args[0]);
                            let (_, min) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_validate_min_len({value}, {min})"),
                            );
                        }
                        "validate_max_len" => {
                            let (_, value) = self.emit_expr(&args[0]);
                            let (_, max) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_validate_max_len({value}, {max})"),
                            );
                        }
                        "validate_int_range" => {
                            let (_, value) = self.emit_expr(&args[0]);
                            let (_, min) = self.emit_expr(&args[1]);
                            let (_, max) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_validate_int_range({value}, {min}, {max})"),
                            );
                        }
                        "validate_email" => {
                            let (_, value) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_validate_email({value})"));
                        }
                        "game_fixed_steps" => {
                            let (_, elapsed_ms) = self.emit_expr(&args[0]);
                            let (_, step_ms) = self.emit_expr(&args[1]);
                            let (_, max_steps) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!(
                                    "mako_game_fixed_steps({elapsed_ms}, {step_ms}, {max_steps})"
                                ),
                            );
                        }
                        "game_fixed_remainder" => {
                            let (_, elapsed_ms) = self.emit_expr(&args[0]);
                            let (_, step_ms) = self.emit_expr(&args[1]);
                            let (_, max_steps) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!(
                                    "mako_game_fixed_remainder({elapsed_ms}, {step_ms}, {max_steps})"
                                ),
                            );
                        }
                        "game_alpha" => {
                            let (_, remainder_ms) = self.emit_expr(&args[0]);
                            let (_, step_ms) = self.emit_expr(&args[1]);
                            let (_, scale) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_game_alpha({remainder_ms}, {step_ms}, {scale})"),
                            );
                        }
                        "game_frame_budget_ok" => {
                            let (_, frame_start_ms) = self.emit_expr(&args[0]);
                            let (_, budget_ms) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_game_frame_budget_ok({frame_start_ms}, {budget_ms})"),
                            );
                        }
                        "fx_from_int" => {
                            let (_, value) = self.emit_expr(&args[0]);
                            let (_, scale) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_fx_from_int({value}, {scale})"),
                            );
                        }
                        "fx_to_int" => {
                            let (_, value) = self.emit_expr(&args[0]);
                            let (_, scale) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_fx_to_int({value}, {scale})"),
                            );
                        }
                        "fx_mul" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, scale) = self.emit_expr(&args[2]);
                            return ("int64_t".into(), format!("mako_fx_mul({a}, {b}, {scale})"));
                        }
                        "fx_div" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, scale) = self.emit_expr(&args[2]);
                            return ("int64_t".into(), format!("mako_fx_div({a}, {b}, {scale})"));
                        }
                        "det_rng_next" => {
                            let (_, state) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_deterministic_rng_next({state})"),
                            );
                        }
                        "det_rng_range" => {
                            let (_, state) = self.emit_expr(&args[0]);
                            let (_, max) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_deterministic_rng_range({state}, {max})"),
                            );
                        }
                        "replay_append" => {
                            let (_, stream) = self.emit_expr(&args[0]);
                            let (_, tick) = self.emit_expr(&args[1]);
                            let (_, input) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("rpa");
                            self.line(&format!(
                                "MakoString {tmp} = mako_replay_append({stream}, {tick}, {input});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "replay_input" => {
                            let (_, stream) = self.emit_expr(&args[0]);
                            let (_, tick) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_replay_input({stream}, {tick})"),
                            );
                        }
                        "ring_new" => {
                            let (_, cap) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_ring_new({cap})"));
                        }
                        "ring_push" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            let (_, value) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_ring_push({id}, {value})"));
                        }
                        "ring_pop" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_ring_pop({id})"));
                        }
                        "ring_peek" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_ring_peek({id})"));
                        }
                        "ring_len" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_ring_len({id})"));
                        }
                        "ring_cap" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_ring_cap({id})"));
                        }
                        "lfq_new" => {
                            let (_, cap) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_lfq_new({cap})"));
                        }
                        "lfq_try_push" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            let (_, value) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_lfq_try_push({id}, {value})"),
                            );
                        }
                        "lfq_try_pop" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_lfq_try_pop({id})"));
                        }
                        "lfq_len" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_lfq_len({id})"));
                        }
                        "sg_gather2" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("sg2");
                            self.line(&format!("MakoString {tmp} = mako_sg_gather2({a}, {b});"));
                            return ("MakoString".into(), tmp);
                        }
                        "sg_gather3" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, c) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("sg3");
                            self.line(&format!(
                                "MakoString {tmp} = mako_sg_gather3({a}, {b}, {c});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "sg_slice" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, offset) = self.emit_expr(&args[1]);
                            let (_, len) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("sgs");
                            self.line(&format!(
                                "MakoString {tmp} = mako_sg_slice({s}, {offset}, {len});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "fsm_rule" => {
                            let (_, from) = self.emit_expr(&args[0]);
                            let (_, event) = self.emit_expr(&args[1]);
                            let (_, to) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("fsmr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_fsm_rule({from}, {event}, {to});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "fsm_is" => {
                            let (_, state) = self.emit_expr(&args[0]);
                            let (_, expected) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_fsm_is({state}, {expected})"),
                            );
                        }
                        "fsm_can" => {
                            let (_, current) = self.emit_expr(&args[0]);
                            let (_, event) = self.emit_expr(&args[1]);
                            let (_, rules) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_fsm_can({current}, {event}, {rules})"),
                            );
                        }
                        "fsm_transition" => {
                            let (_, current) = self.emit_expr(&args[0]);
                            let (_, event) = self.emit_expr(&args[1]);
                            let (_, rules) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("fsmt");
                            self.line(&format!(
                                "MakoString {tmp} = mako_fsm_transition({current}, {event}, {rules});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "frame_alloc_new" => {
                            let (_, cap) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_frame_alloc_new({cap})"));
                        }
                        "frame_alloc" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            let (_, bytes) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_frame_alloc({id}, {bytes})"));
                        }
                        "frame_reset" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_frame_reset({id})"));
                        }
                        "frame_used" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_frame_used({id})"));
                        }
                        "frame_cap" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_frame_cap({id})"));
                        }
                        "obj_pool_new" => {
                            let (_, cap) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_obj_pool_new({cap})"));
                        }
                        "obj_acquire" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_obj_acquire({id})"));
                        }
                        "obj_release" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            let (_, obj) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_obj_release({id}, {obj})"));
                        }
                        "obj_available" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_obj_available({id})"));
                        }
                        "obj_pool_cap" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_obj_pool_cap({id})"));
                        }
                        "alloc_track_reset" => {
                            return ("int64_t".into(), "mako_alloc_track_reset()".into());
                        }
                        "alloc_track_alloc" => {
                            let (_, bytes) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_alloc_track_alloc({bytes})"));
                        }
                        "alloc_track_free" => {
                            let (_, bytes) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_alloc_track_free({bytes})"));
                        }
                        "alloc_live_bytes" => {
                            return ("int64_t".into(), "mako_alloc_track_live_bytes()".into());
                        }
                        "alloc_high_bytes" => {
                            return ("int64_t".into(), "mako_alloc_track_high_bytes()".into());
                        }
                        "alloc_report_json" => {
                            let tmp = self.fresh("alr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_alloc_track_report_json();"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "leak_mark" => {
                            return ("int64_t".into(), "mako_leak_mark()".into());
                        }
                        "leak_bytes_since" => {
                            let (_, mark) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_leak_bytes_since({mark})"));
                        }
                        "leak_detected" => {
                            let (_, mark) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_leak_detected({mark})"));
                        }
                        "leak_assert_clear" => {
                            let (_, mark) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_leak_assert_clear({mark})"));
                        }
                        "leak_report_json" => {
                            let (_, mark) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("lrj");
                            self.line(&format!(
                                "MakoString {tmp} = mako_leak_report_json({mark});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "leak_scope_enter" => {
                            return ("int64_t".into(), "mako_leak_scope_enter()".into());
                        }
                        "leak_scope_exit" => {
                            return ("int64_t".into(), "mako_leak_scope_exit()".into());
                        }
                        "leak_check" => {
                            return ("int64_t".into(), "mako_leak_check()".into());
                        }
                        "leak_assert_scope" => {
                            return ("int64_t".into(), "mako_leak_assert_scope()".into());
                        }
                        "checked_add" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_checked_add({a}, {b})"));
                        }
                        "checked_sub" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_checked_sub({a}, {b})"));
                        }
                        "checked_mul" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_checked_mul({a}, {b})"));
                        }
                        "would_overflow_add" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_would_overflow_add({a}, {b})"),
                            );
                        }
                        "would_overflow_sub" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_would_overflow_sub({a}, {b})"),
                            );
                        }
                        "would_overflow_mul" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_would_overflow_mul({a}, {b})"),
                            );
                        }
                        "signal_on_term" => {
                            return ("int64_t".into(), "mako_signal_on_term()".into());
                        }
                        "register_listener" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_register_listener({f})"));
                        }
                        "close_listeners" => {
                            return ("int64_t".into(), "mako_close_listeners()".into());
                        }
                        "server_shutdown_begin" => {
                            let (_, ms) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_server_shutdown_begin({ms})"),
                            );
                        }
                        "server_drain" => {
                            let (_, ms) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_server_drain({ms})"));
                        }
                        "shutdown_requested" => {
                            return ("int64_t".into(), "mako_shutdown_requested()".into());
                        }
                        "should_stop_accepting" => {
                            return ("int64_t".into(), "mako_should_stop_accepting()".into());
                        }
                        "install_graceful_shutdown" => {
                            let (_, ms) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_install_graceful_shutdown({ms})"),
                            );
                        }
                        "trace_id" => {
                            let tmp = self.fresh("tid");
                            self.line(&format!("MakoString {tmp} = mako_trace_id();"));
                            return ("MakoString".into(), tmp);
                        }
                        "trace_current" => {
                            let tmp = self.fresh("tcur");
                            self.line(&format!("MakoString {tmp} = mako_trace_current();"));
                            return ("MakoString".into(), tmp);
                        }
                        "trace_set" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_trace_set({s})"));
                        }
                        "trace_clear" => {
                            return ("int64_t".into(), "mako_trace_clear()".into());
                        }
                        "trace_begin" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_trace_begin({n})"));
                        }
                        "trace_end" => {
                            return ("int64_t".into(), "mako_trace_end()".into());
                        }
                        "trace_log" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_trace_log({m})"));
                        }
                        "ecs_world_new" => {
                            let (_, cap) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_ecs_world_new({cap})"));
                        }
                        "ecs_spawn" => {
                            let (_, world) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_ecs_spawn({world})"));
                        }
                        "ecs_alive" => {
                            let (_, world) = self.emit_expr(&args[0]);
                            let (_, entity) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_ecs_alive({world}, {entity})"),
                            );
                        }
                        "ecs_despawn" => {
                            let (_, world) = self.emit_expr(&args[0]);
                            let (_, entity) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_ecs_despawn({world}, {entity})"),
                            );
                        }
                        "ecs_add" => {
                            let (_, world) = self.emit_expr(&args[0]);
                            let (_, entity) = self.emit_expr(&args[1]);
                            let (_, component) = self.emit_expr(&args[2]);
                            let (_, value) = self.emit_expr(&args[3]);
                            return (
                                "int64_t".into(),
                                format!("mako_ecs_add({world}, {entity}, {component}, {value})"),
                            );
                        }
                        "ecs_set" => {
                            let (_, world) = self.emit_expr(&args[0]);
                            let (_, entity) = self.emit_expr(&args[1]);
                            let (_, component) = self.emit_expr(&args[2]);
                            let (_, value) = self.emit_expr(&args[3]);
                            return (
                                "int64_t".into(),
                                format!("mako_ecs_set({world}, {entity}, {component}, {value})"),
                            );
                        }
                        "ecs_has" => {
                            let (_, world) = self.emit_expr(&args[0]);
                            let (_, entity) = self.emit_expr(&args[1]);
                            let (_, component) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_ecs_has({world}, {entity}, {component})"),
                            );
                        }
                        "ecs_get" => {
                            let (_, world) = self.emit_expr(&args[0]);
                            let (_, entity) = self.emit_expr(&args[1]);
                            let (_, component) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_ecs_get({world}, {entity}, {component})"),
                            );
                        }
                        "ecs_remove" => {
                            let (_, world) = self.emit_expr(&args[0]);
                            let (_, entity) = self.emit_expr(&args[1]);
                            let (_, component) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_ecs_remove({world}, {entity}, {component})"),
                            );
                        }
                        "ecs_query_count" => {
                            let (_, world) = self.emit_expr(&args[0]);
                            let (_, component) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_ecs_query_count({world}, {component})"),
                            );
                        }
                        "ecs_query_first" => {
                            let (_, world) = self.emit_expr(&args[0]);
                            let (_, component) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_ecs_query_first({world}, {component})"),
                            );
                        }
                        "ecs_archetype" => {
                            let (_, world) = self.emit_expr(&args[0]);
                            let (_, entity) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_ecs_archetype({world}, {entity})"),
                            );
                        }
                        "ecs_system_add" => {
                            let (_, world) = self.emit_expr(&args[0]);
                            let (_, component) = self.emit_expr(&args[1]);
                            let (_, delta) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_ecs_system_add({world}, {component}, {delta})"),
                            );
                        }
                        "cookie_get" => {
                            let (_, header) = self.emit_expr(&args[0]);
                            let (_, name) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ckg");
                            self.line(&format!(
                                "MakoString {tmp} = mako_cookie_get({header}, {name});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "cookie_make" => {
                            let (_, name) = self.emit_expr(&args[0]);
                            let (_, value) = self.emit_expr(&args[1]);
                            let (_, max_age) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("ckm");
                            self.line(&format!(
                                "MakoString {tmp} = mako_cookie_make({name}, {value}, {max_age});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "session_id_new" => {
                            let tmp = self.fresh("sid");
                            self.line(&format!("MakoString {tmp} = mako_session_id_new();"));
                            return ("MakoString".into(), tmp);
                        }
                        "csrf_token" => {
                            let tmp = self.fresh("csrf");
                            self.line(&format!("MakoString {tmp} = mako_csrf_token();"));
                            return ("MakoString".into(), tmp);
                        }
                        "csrf_check" => {
                            let (_, expected) = self.emit_expr(&args[0]);
                            let (_, submitted) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_csrf_check({expected}, {submitted})"),
                            );
                        }
                        "auth_bearer" => {
                            let (_, authorization) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ab");
                            self.line(&format!(
                                "MakoString {tmp} = mako_auth_bearer({authorization});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "auth_check_bearer" => {
                            let (_, authorization) = self.emit_expr(&args[0]);
                            let (_, expected) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_auth_check_bearer({authorization}, {expected})"),
                            );
                        }
                        "auth_basic_header" => {
                            let (_, user) = self.emit_expr(&args[0]);
                            let (_, pass) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("abh");
                            self.line(&format!(
                                "MakoString {tmp} = mako_auth_basic_header({user}, {pass});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "auth_check_basic" => {
                            let (_, authorization) = self.emit_expr(&args[0]);
                            let (_, user) = self.emit_expr(&args[1]);
                            let (_, pass) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_auth_check_basic({authorization}, {user}, {pass})"),
                            );
                        }
                        "auth_role_has" => {
                            let (_, roles) = self.emit_expr(&args[0]);
                            let (_, role) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_auth_role_has({roles}, {role})"),
                            );
                        }
                        "authz_allow_role" => {
                            let (_, roles) = self.emit_expr(&args[0]);
                            let (_, required) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_authz_allow_role({roles}, {required})"),
                            );
                        }
                        "auth_token_sign" => {
                            let (_, subject) = self.emit_expr(&args[0]);
                            let (_, secret) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ats");
                            self.line(&format!(
                                "MakoString {tmp} = mako_auth_token_sign({subject}, {secret});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "auth_token_subject" => {
                            let (_, token) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("atsub");
                            self.line(&format!(
                                "MakoString {tmp} = mako_auth_token_subject({token});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "auth_token_check" => {
                            let (_, token) = self.emit_expr(&args[0]);
                            let (_, secret) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_auth_token_check({token}, {secret})"),
                            );
                        }
                        "auth_session_cookie" => {
                            let (_, header) = self.emit_expr(&args[0]);
                            let (_, name) = self.emit_expr(&args[1]);
                            let (_, expected) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_auth_session_cookie({header}, {name}, {expected})"),
                            );
                        }
                        "rate_allow" => {
                            let (_, key) = self.emit_expr(&args[0]);
                            let (_, limit) = self.emit_expr(&args[1]);
                            let (_, window_ms) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_rate_allow({key}, {limit}, {window_ms})"),
                            );
                        }
                        "rate_remaining" => {
                            let (_, key) = self.emit_expr(&args[0]);
                            let (_, limit) = self.emit_expr(&args[1]);
                            let (_, window_ms) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_rate_remaining({key}, {limit}, {window_ms})"),
                            );
                        }
                        "http_content_encoding" => {
                            let (_, accept_encoding) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hce");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_content_encoding({accept_encoding});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_compress_if_accepted" => {
                            let (_, body) = self.emit_expr(&args[0]);
                            let (_, accept_encoding) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("hca");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_compress_if_accepted({body}, {accept_encoding});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "cache_put" => {
                            let (_, key) = self.emit_expr(&args[0]);
                            let (_, value) = self.emit_expr(&args[1]);
                            let (_, ttl_ms) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_cache_put({key}, {value}, {ttl_ms})"),
                            );
                        }
                        "cache_get" => {
                            let (_, key) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("cg");
                            self.line(&format!("MakoString {tmp} = mako_cache_get({key});"));
                            return ("MakoString".into(), tmp);
                        }
                        "cache_has" => {
                            let (_, key) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_cache_has({key})"));
                        }
                        "job_schedule" => {
                            let (_, name) = self.emit_expr(&args[0]);
                            let (_, delay_ms) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_job_schedule({name}, {delay_ms})"),
                            );
                        }
                        "job_due" => {
                            let (_, name) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_job_due({name})"));
                        }
                        "job_delay_ms" => {
                            let (_, name) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_job_delay_ms({name})"));
                        }
                        "job_cancel" => {
                            let (_, name) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_job_cancel({name})"));
                        }
                        "conn_pool_slot" => {
                            let (_, key) = self.emit_expr(&args[0]);
                            let (_, pool_size) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_conn_pool_slot({key}, {pool_size})"),
                            );
                        }
                        "conn_pool_next" => {
                            let (_, pool_size) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_conn_pool_next({pool_size})"),
                            );
                        }
                        "lb_pick2" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, key) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("lb");
                            self.line(&format!(
                                "MakoString {tmp} = mako_lb_pick2({a}, {b}, {key});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "lb_pick3" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, c) = self.emit_expr(&args[2]);
                            let (_, key) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("lb");
                            self.line(&format!(
                                "MakoString {tmp} = mako_lb_pick3({a}, {b}, {c}, {key});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "slog_redact" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("sr");
                            self.line(&format!("MakoString {tmp} = mako_slog_redact({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "slog_with_redacted" => {
                            let (_, level) = self.emit_expr(&args[0]);
                            let (_, msg) = self.emit_expr(&args[1]);
                            let (_, key) = self.emit_expr(&args[2]);
                            self.line(&format!("mako_slog_with_redacted({level}, {msg}, {key});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "metric_inc" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_metric_inc({id});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "metric_add" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            let (_, delta) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_metric_add({id}, {delta});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "metric_get" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_metric_get({id})"));
                        }
                        "gauge_set" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            let (_, value) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_gauge_set({id}, {value});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "gauge_add" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            let (_, delta) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_gauge_add({id}, {delta});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "gauge_get" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_gauge_get({id})"));
                        }
                        "hist_observe" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            let (_, value) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_hist_observe({id}, {value});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "hist_count" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_hist_count({id})"));
                        }
                        "hist_sum" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_hist_sum({id})"));
                        }
                        "hist_avg" => {
                            let (_, id) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_hist_avg({id})"));
                        }
                        "metrics_export" => {
                            return ("MakoString".into(), "mako_metrics_export()".into());
                        }
                        "share_int" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("si");
                            self.line(&format!("MakoShareInt {tmp} = mako_share_int({v});"));
                            return ("MakoShareInt".into(), tmp);
                        }
                        "share_clone" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("sc");
                            self.line(&format!("MakoShareInt {tmp} = mako_share_clone({s});"));
                            return ("MakoShareInt".into(), tmp);
                        }
                        "share_get" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_share_get({s})"));
                        }
                        "share_set" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_share_set({s}, {v});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "share_drop" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            if let Expr::Ident(name) = &args[0] {
                                self.note_share_dropped(name);
                            }
                            self.line(&format!("mako_share_drop({s});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "slice_ints" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, st) = self.emit_expr(&args[1]);
                            let (_, en) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("sl");
                            self.line(&format!(
                                "MakoSlice {tmp} = mako_slice_ints({a}, {st}, {en});"
                            ));
                            return ("MakoSlice".into(), tmp);
                        }
                        "slice_len" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_slice_len({s})"));
                        }
                        "slice_get" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_slice_get({s}, {i})"));
                        }
                        "as_bytes" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ab");
                            self.line(&format!("MakoByteArray {tmp} = mako_as_bytes({s});"));
                            return ("MakoByteArray".into(), tmp);
                        }
                        "bytes_as_str" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("bas");
                            self.line(&format!("MakoString {tmp} = mako_bytes_as_str({a});"));
                            return ("MakoString".into(), tmp);
                        }
                        "bytes_is_view" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_bytes_is_view({a})"));
                        }
                        "bytes_view" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, lo) = self.emit_expr(&args[1]);
                            let (_, hi) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("bv");
                            self.line(&format!(
                                "MakoByteArray {tmp} = mako_bytes_view(({s}.data ? {s}.data : \"\") + ({lo}), (size_t)(({hi}) - ({lo})));"
                            ));
                            return ("MakoByteArray".into(), tmp);
                        }
                        "buf_get" => {
                            let (_, need) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("buf");
                            self.line(&format!("MakoByteArray {tmp} = mako_buf_get({need});"));
                            return ("MakoByteArray".into(), tmp);
                        }
                        "buf_put" => {
                            let (_, buf) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_buf_put({buf});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "simd_xor_bytes" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_simd_xor_bytes({a})"));
                        }
                        "http_req_method" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hrm");
                            self.line(&format!("MakoString {tmp} = mako_http_method({c});"));
                            return ("MakoString".into(), tmp);
                        }
                        "http_req_path" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hrp");
                            self.line(&format!("MakoString {tmp} = mako_http_path({c});"));
                            return ("MakoString".into(), tmp);
                        }
                        "http_req_body" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hrb");
                            self.line(&format!("MakoString {tmp} = mako_http_body({c});"));
                            return ("MakoString".into(), tmp);
                        }
                        "http_request_parse" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hrq");
                            self.line(&format!(
                                "MakoHttpRequest {tmp} = mako_http_request_parse({s});"
                            ));
                            return ("MakoHttpRequest".into(), tmp);
                        }
                        "http_request_from_conn" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hrc");
                            self.line(&format!(
                                "MakoHttpRequest {tmp} = mako_http_request_from_conn({c});"
                            ));
                            return ("MakoHttpRequest".into(), tmp);
                        }
                        "http_request_method" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hrm2");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_request_method({r});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_request_path" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hrp2");
                            self.line(&format!("MakoString {tmp} = mako_http_request_path({r});"));
                            return ("MakoString".into(), tmp);
                        }
                        "http_request_body" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hrb2");
                            self.line(&format!("MakoString {tmp} = mako_http_request_body({r});"));
                            return ("MakoString".into(), tmp);
                        }
                        "http_route_match" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            let (_, p) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("hrt");
                            self.line(&format!(
                                "bool {tmp} = mako_http_route_match({r}, {m}, {p});"
                            ));
                            return ("bool".into(), tmp);
                        }
                        "http_route_param" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, n) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("hrtp");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_route_param({r}, {p}, {n});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "router_new" => {
                            return ("int64_t".into(), "mako_router_new()".into());
                        }
                        "router_group" => {
                            let (_, router) = self.emit_expr(&args[0]);
                            let (_, prefix) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_router_group({router}, {prefix})"),
                            );
                        }
                        "router_add" => {
                            let (_, router) = self.emit_expr(&args[0]);
                            let (_, method) = self.emit_expr(&args[1]);
                            let (_, pattern) = self.emit_expr(&args[2]);
                            let (_, handler) = self.emit_expr(&args[3]);
                            return (
                                "int64_t".into(),
                                format!(
                                    "mako_router_add({router}, {method}, {pattern}, {handler})"
                                ),
                            );
                        }
                        "router_match" => {
                            let (_, router) = self.emit_expr(&args[0]);
                            let (_, req) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("rmat");
                            self.line(&format!(
                                "MakoString {tmp} = mako_router_match({router}, {req});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "router_match_path" => {
                            let (_, router) = self.emit_expr(&args[0]);
                            let (_, method) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("rmp");
                            self.line(&format!(
                                "MakoString {tmp} = mako_router_match_path({router}, {method}, {path});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "router_param" => {
                            let (_, router) = self.emit_expr(&args[0]);
                            let (_, req) = self.emit_expr(&args[1]);
                            let (_, name) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("rpar");
                            self.line(&format!(
                                "MakoString {tmp} = mako_router_param({router}, {req}, {name});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "router_count" => {
                            let (_, router) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_router_count({router})"));
                        }
                        "reqctx_new" => {
                            return ("int64_t".into(), "mako_reqctx_new()".into());
                        }
                        "reqctx_set" => {
                            let (_, ctx) = self.emit_expr(&args[0]);
                            let (_, key) = self.emit_expr(&args[1]);
                            let (_, value) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_reqctx_set({ctx}, {key}, {value})"),
                            );
                        }
                        "reqctx_get" => {
                            let (_, ctx) = self.emit_expr(&args[0]);
                            let (_, key) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("rcg");
                            self.line(&format!(
                                "MakoString {tmp} = mako_reqctx_get_value({ctx}, {key});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "reqctx_has" => {
                            let (_, ctx) = self.emit_expr(&args[0]);
                            let (_, key) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_reqctx_has({ctx}, {key})"));
                        }
                        "reqctx_count" => {
                            let (_, ctx) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_reqctx_count({ctx})"));
                        }
                        "middleware_allow_methods" => {
                            let (_, req) = self.emit_expr(&args[0]);
                            let (_, methods) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_middleware_allow_methods({req}, {methods})"),
                            );
                        }
                        "middleware_next" => {
                            let (_, ctx) = self.emit_expr(&args[0]);
                            let (_, name) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_middleware_next({ctx}, {name})"),
                            );
                        }
                        "middleware_ran" => {
                            let (_, ctx) = self.emit_expr(&args[0]);
                            let (_, name) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_middleware_ran({ctx}, {name})"),
                            );
                        }
                        "middleware_trace" => {
                            let (_, ctx) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("mwt");
                            self.line(&format!("MakoString {tmp} = mako_middleware_trace({ctx});"));
                            return ("MakoString".into(), tmp);
                        }
                        "middleware_require_context" => {
                            let (_, ctx) = self.emit_expr(&args[0]);
                            let (_, key) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_middleware_require_context({ctx}, {key})"),
                            );
                        }
                        "http_health_json" => {
                            let (_, service) = self.emit_expr(&args[0]);
                            let (_, ready) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("hhj");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_health_json({service}, {ready});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_respond_health" => {
                            let (_, conn) = self.emit_expr(&args[0]);
                            let (_, service) = self.emit_expr(&args[1]);
                            let (_, ready) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("hrh");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http_respond_health({conn}, {service}, {ready});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "buf_reader_new" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("br");
                            self.line(&format!("MakoBufReader *{tmp} = mako_buf_reader_new({p});"));
                            return ("MakoBufReader*".into(), tmp);
                        }
                        "buf_reader_from_string" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("brs");
                            self.line(&format!(
                                "MakoBufReader *{tmp} = mako_buf_reader_from_string({s});"
                            ));
                            return ("MakoBufReader*".into(), tmp);
                        }
                        "buf_read_line" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("brl");
                            self.line(&format!("MakoString {tmp} = mako_buf_read_line({r});"));
                            return ("MakoString".into(), tmp);
                        }
                        "buf_read" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("brd");
                            self.line(&format!("MakoString {tmp} = mako_buf_read({r}, {n});"));
                            return ("MakoString".into(), tmp);
                        }
                        "buf_reader_close" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_buf_reader_close({r})"));
                        }
                        "buf_writer_new" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("bw");
                            self.line(&format!("MakoBufWriter *{tmp} = mako_buf_writer_new({p});"));
                            return ("MakoBufWriter*".into(), tmp);
                        }
                        "buf_write" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_buf_write({w}, {s})"));
                        }
                        "buf_write_byte" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_buf_write_byte({w}, {v})"));
                        }
                        "buf_flush" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_buf_flush({w})"));
                        }
                        "buf_writer_close" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_buf_writer_close({w})"));
                        }
                        "sql_open_sqlite" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("sq");
                            self.line(&format!("MakoSqlDB {tmp} = mako_sql_open_sqlite({p});"));
                            return ("MakoSqlDB".into(), tmp);
                        }
                        "sql_open_postgres" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("sp");
                            self.line(&format!("MakoSqlDB {tmp} = mako_sql_open_postgres({u});"));
                            return ("MakoSqlDB".into(), tmp);
                        }
                        "sql_ok" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_sql_ok({d})"));
                        }
                        "sql_close" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_sql_close({d})"));
                        }
                        "sql_query_int" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, q) = self.emit_expr(&args[1]);
                            let (_, a) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_sql_query_int({d}, {q}, {a})"),
                            );
                        }
                        "sql_exec" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, q) = self.emit_expr(&args[1]);
                            let (_, a) = self.emit_expr(&args[2]);
                            return ("int64_t".into(), format!("mako_sql_exec({d}, {q}, {a})"));
                        }
                        "sql_exec_plain" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, q) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_sql_exec_plain({d}, {q})"));
                        }
                        "sql_exec_str4" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, q) = self.emit_expr(&args[1]);
                            let (_, p1) = self.emit_expr(&args[2]);
                            let (_, p2) = self.emit_expr(&args[3]);
                            let (_, p3) = self.emit_expr(&args[4]);
                            let (_, p4) = self.emit_expr(&args[5]);
                            return ("int64_t".into(), format!("mako_sql_exec_str4({d}, {q}, {p1}, {p2}, {p3}, {p4})"));
                        }
                        "sql_query_str" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, q) = self.emit_expr(&args[1]);
                            let (_, p1) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("sqs");
                            self.line(&format!("MakoString {tmp} = mako_sql_query_str({d}, {q}, {p1});"));
                            return ("MakoString".into(), tmp);
                        }
                        "sql_begin" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_sql_begin({d})"));
                        }
                        "sql_commit" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_sql_commit({d})"));
                        }
                        "sql_rollback" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_sql_rollback({d})"));
                        }
                        "sql_prepare" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, q) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_sql_prepare({d}, {q})"));
                        }
                        "sql_stmt_query_int" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, a) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_sql_stmt_query_int({s}, {a})"),
                            );
                        }
                        "sql_stmt_exec" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, a) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_sql_stmt_exec({s}, {a})"));
                        }
                        "sql_stmt_close" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_sql_stmt_close({s})"));
                        }
                        "sql_migration_applied" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_sql_migration_applied({d}, {v})"),
                            );
                        }
                        "sql_migrate" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            let (_, up) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_sql_migrate({d}, {v}, {up})"),
                            );
                        }
                        "sql_check_typed" => {
                            let (_, schema) = self.emit_expr(&args[0]);
                            let (_, sql) = self.emit_expr(&args[1]);
                            let (_, params) = self.emit_expr(&args[2]);
                            let (_, result) = self.emit_expr(&args[3]);
                            return (
                                "int64_t".into(),
                                format!(
                                    "mako_sql_check_typed({schema}, {sql}, {params}, {result})"
                                ),
                            );
                        }
                        "sql_pool_open_sqlite" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_sql_pool_open_sqlite({p}, {n})"),
                            );
                        }
                        "sql_pool_open_postgres" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_sql_pool_open_postgres({u}, {n})"),
                            );
                        }
                        "sql_pool_ok" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_sql_pool_ok({p})"));
                        }
                        "sql_pool_size" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_sql_pool_size({p})"));
                        }
                        "sql_pool_opened" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_sql_pool_opened({p})"));
                        }
                        "sql_pool_next_slot" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_sql_pool_next_slot({p})"));
                        }
                        "sql_pool_query_int" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, q) = self.emit_expr(&args[1]);
                            let (_, a) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_sql_pool_query_int({p}, {q}, {a})"),
                            );
                        }
                        "sql_pool_exec" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, q) = self.emit_expr(&args[1]);
                            let (_, a) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_sql_pool_exec({p}, {q}, {a})"),
                            );
                        }
                        "sql_pool_close" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_sql_pool_close({p})"));
                        }
                        "wait_group_new" => {
                            let tmp = self.fresh("wg");
                            self.line(&format!("MakoWaitGroup *{tmp} = mako_wait_group_new();"));
                            return ("MakoWaitGroup*".into(), tmp);
                        }
                        "wait_group_add" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_wait_group_add({w}, {n});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "wait_group_done" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_wait_group_done({w});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "wait_group_wait" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_wait_group_wait({w});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "flag_string" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("fs");
                            self.line(&format!("MakoString {tmp} = mako_flag_string({n}, {d});"));
                            return ("MakoString".into(), tmp);
                        }
                        "flag_int" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_flag_int({n}, {d})"));
                        }
                        "flag_bool" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_flag_bool({n}, {d})"));
                        }
                        "exec_output" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("eo");
                            self.line(&format!("MakoString {tmp} = mako_exec_output({c});"));
                            return ("MakoString".into(), tmp);
                        }
                        "exec_run" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_exec_run({c})"));
                        }
                        "url_query_escape" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ue");
                            self.line(&format!("MakoString {tmp} = mako_url_query_escape({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "url_scheme" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("us");
                            self.line(&format!("MakoString {tmp} = mako_url_scheme({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "url_host" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("uh");
                            self.line(&format!("MakoString {tmp} = mako_url_host({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "url_path" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("up");
                            self.line(&format!("MakoString {tmp} = mako_url_path({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "url_query" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("uq");
                            self.line(&format!("MakoString {tmp} = mako_url_query({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "csv_split_line" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("cs");
                            self.line(&format!("MakoStrArray {tmp} = mako_csv_split_line({s});"));
                            return ("MakoStrArray".into(), tmp);
                        }
                        "csv_join_row" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("cj");
                            self.line(&format!("MakoString {tmp} = mako_csv_join_row({a});"));
                            return ("MakoString".into(), tmp);
                        }
                        "xml_escape" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("xe");
                            self.line(&format!("MakoString {tmp} = mako_xml_escape({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "html_escape" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("he");
                            self.line(&format!("MakoString {tmp} = mako_html_escape({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "xml_tag_text" => {
                            let (_, x) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("xt");
                            self.line(&format!("MakoString {tmp} = mako_xml_tag_text({x}, {t});"));
                            return ("MakoString".into(), tmp);
                        }
                        "gzip_compress" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gz");
                            self.line(&format!("MakoString {tmp} = mako_gzip_compress({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "gzip_decompress" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gzd");
                            self.line(&format!("MakoString {tmp} = mako_gzip_decompress({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "gzip_available" => {
                            return ("int64_t".into(), "mako_gzip_available()".into());
                        }
                        "tar_write_file" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, d) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_tar_write_file({p}, {n}, {d})"),
                            );
                        }
                        "tar_first_name" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tn");
                            self.line(&format!("MakoString {tmp} = mako_tar_first_name({p});"));
                            return ("MakoString".into(), tmp);
                        }
                        "mime_type" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("mt");
                            self.line(&format!("MakoString {tmp} = mako_mime_type_by_ext({p});"));
                            return ("MakoString".into(), tmp);
                        }
                        "context_with_timeout" => {
                            let (_, ms) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_context_with_timeout_ms({ms})"),
                            );
                        }
                        "context_expired" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_context_expired({d})"));
                        }
                        "context_remaining" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_context_remaining_ms({d})"));
                        }
                        "bytes_buffer" => {
                            let tmp = self.fresh("bb");
                            self.line(&format!(
                                "MakoBytesBuffer *{tmp} = mako_bytes_buffer_new();"
                            ));
                            return ("MakoBytesBuffer*".into(), tmp);
                        }
                        "bytes_buffer_write" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_bytes_buffer_write({b}, {s});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "bytes_buffer_string" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("bbs");
                            self.line(&format!(
                                "MakoString {tmp} = mako_bytes_buffer_string({b});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "bytes_buffer_len" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_bytes_buffer_len({b})"));
                        }
                        "bytes_buffer_reset" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_bytes_buffer_reset({b});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "rand_seed" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_rand_seed({s});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "rand_intn" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_rand_intn({n})"));
                        }
                        "rand_float" => {
                            return ("double".into(), "mako_rand_float()".into());
                        }
                        "template_execute" => {
                            let (_, t) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let (_, v) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("te");
                            self.line(&format!(
                                "MakoString {tmp} = mako_template_execute({t}, {k}, {v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "base32_encode" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("b32");
                            self.line(&format!("MakoString {tmp} = mako_base32_encode({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "sha1" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("s1");
                            self.line(&format!("MakoString {tmp} = mako_sha1_hex({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "sha512" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("s512");
                            self.line(&format!("MakoString {tmp} = mako_sha512_hex({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "lookup_host" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("lh");
                            self.line(&format!("MakoString {tmp} = mako_lookup_host({h});"));
                            return ("MakoString".into(), tmp);
                        }
                        "dns_lookup_count" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_dns_lookup_count({h})"));
                        }
                        "dns_lookup_all" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("dla");
                            self.line(&format!("MakoString {tmp} = mako_dns_lookup_all({h});"));
                            return ("MakoString".into(), tmp);
                        }
                        "dns_lookup_ipv4" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("dl4");
                            self.line(&format!("MakoString {tmp} = mako_dns_lookup_ipv4({h});"));
                            return ("MakoString".into(), tmp);
                        }
                        "dns_lookup_ipv6" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("dl6");
                            self.line(&format!("MakoString {tmp} = mako_dns_lookup_ipv6({h});"));
                            return ("MakoString".into(), tmp);
                        }
                        "parse_ip_ok" => {
                            let (_, ip) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_parse_ip_ok({ip})"));
                        }
                        "dns_ip_family" => {
                            let (_, ip) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_dns_ip_family({ip})"));
                        }
                        "dns_is_loopback" => {
                            let (_, ip) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_dns_is_loopback({ip})"));
                        }
                        "dns_is_private" => {
                            let (_, ip) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_dns_is_private({ip})"));
                        }
                        "dns_normalize_host" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("dnh");
                            self.line(&format!("MakoString {tmp} = mako_dns_normalize_host({h});"));
                            return ("MakoString".into(), tmp);
                        }
                        "dns_join_host_port" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("djhp");
                            self.line(&format!(
                                "MakoString {tmp} = mako_dns_join_host_port({h}, {p});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "dns_split_host" => {
                            let (_, hp) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("dsh");
                            self.line(&format!("MakoString {tmp} = mako_dns_split_host({hp});"));
                            return ("MakoString".into(), tmp);
                        }
                        "dns_split_port" => {
                            let (_, hp) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_dns_split_port({hp})"));
                        }
                        "signal_notify" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_signal_notify({s})"));
                        }
                        "signal_received" => {
                            return ("int64_t".into(), "mako_signal_received()".into());
                        }
                        "signal_watch" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_signal_watch({n})"));
                        }
                        "signal_fired" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_signal_fired({n})"));
                        }
                        "signal_ignore" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_signal_ignore({n})"));
                        }
                        "watch_available" => {
                            return ("int64_t".into(), "mako_watch_available()".into());
                        }
                        "watch_new" => {
                            let tmp = self.fresh("wch");
                            self.line(&format!("void *{tmp} = mako_watch_new();"));
                            return ("void*".into(), tmp);
                        }
                        "watch_add" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("wad");
                            self.line(&format!("int64_t {tmp} = mako_watch_add({w}, {p});"));
                            return ("int64_t".into(), tmp);
                        }
                        "watch_poll" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("wpl");
                            self.line(&format!("MakoString {tmp} = mako_watch_poll({w}, {t});"));
                            return ("MakoString".into(), tmp);
                        }
                        "watch_close" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_watch_close({w});"));
                            return ("int64_t".into(), "0".into());
                        }
                        "atomic_new" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("at");
                            self.line(&format!("MakoAtomicInt *{tmp} = mako_atomic_new({v});"));
                            return ("MakoAtomicInt*".into(), tmp);
                        }
                        "atomic_load" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_atomic_load({p})"));
                        }
                        "atomic_store" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_atomic_store({p}, {v});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "atomic_add" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_atomic_add({p}, {d})"));
                        }
                        "atomic_cas" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, o) = self.emit_expr(&args[1]);
                            let (_, n) = self.emit_expr(&args[2]);
                            return ("int64_t".into(), format!("mako_atomic_cas({p}, {o}, {n})"));
                        }
                        "utf8_valid" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_utf8_valid({s})"));
                        }
                        "utf8_rune_len" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_utf8_rune_len({r})"));
                        }
                        "filepath_walk" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("fw");
                            self.line(&format!("MakoStrArray {tmp} = mako_filepath_walk({p});"));
                            return ("MakoStrArray".into(), tmp);
                        }
                        "slices_reverse" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("sr");
                            self.line(&format!(
                                "MakoIntArray {tmp} = mako_slices_reverse_ints({a});"
                            ));
                            return ("MakoIntArray".into(), tmp);
                        }
                        "slices_unique" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("su");
                            self.line(&format!(
                                "MakoIntArray {tmp} = mako_slices_unique_ints({a});"
                            ));
                            return ("MakoIntArray".into(), tmp);
                        }
                        "embed_file" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ef");
                            self.line(&format!("MakoString {tmp} = mako_embed_file({p});"));
                            return ("MakoString".into(), tmp);
                        }
                        "filepath_walk_n" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("fwn");
                            self.line(&format!(
                                "MakoStrArray {tmp} = mako_filepath_walk_n({p}, {d});"
                            ));
                            return ("MakoStrArray".into(), tmp);
                        }
                        "zip_write_file" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, d) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_zip_write_file({p}, {n}, {d})"),
                            );
                        }
                        "zip_first_name" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("zfn");
                            self.line(&format!("MakoString {tmp} = mako_zip_first_name({p});"));
                            return ("MakoString".into(), tmp);
                        }
                        "zip_read_file" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("zrf");
                            self.line(&format!("MakoString {tmp} = mako_zip_read_file({p}, {n});"));
                            return ("MakoString".into(), tmp);
                        }
                        "png_available" => {
                            return ("int64_t".into(), "mako_png_available()".into());
                        }
                        "png_encode_gray" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            let (_, h) = self.emit_expr(&args[1]);
                            let (_, px) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("png");
                            self.line(&format!(
                                "MakoString {tmp} = mako_png_encode_gray({w}, {h}, {px});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "png_encode_rgb" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            let (_, h) = self.emit_expr(&args[1]);
                            let (_, px) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("png");
                            self.line(&format!(
                                "MakoString {tmp} = mako_png_encode_rgb({w}, {h}, {px});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "png_decode_gray" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pngd");
                            self.line(&format!("MakoString {tmp} = mako_png_decode_gray({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "png_width" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_png_width({s})"));
                        }
                        "png_height" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_png_height({s})"));
                        }
                        "maps_keys" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("mk");
                            self.line(&format!("MakoStrArray {tmp} = mako_maps_keys_si({m});"));
                            return ("MakoStrArray".into(), tmp);
                        }
                        "maps_values" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("mv");
                            self.line(&format!("MakoIntArray {tmp} = mako_maps_values_si({m});"));
                            return ("MakoIntArray".into(), tmp);
                        }
                        "maps_clear" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_maps_clear_si({m});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "maps_clone" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("mc");
                            self.line(&format!("MakoMapSI *{tmp} = mako_maps_clone_si({m});"));
                            return ("MakoMapSI*".into(), tmp);
                        }
                        "maps_equal" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_maps_equal_si({a}, {b})"));
                        }
                        "maps_copy" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            self.line(&format!("mako_maps_copy_si({d}, {s});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "reflect_type_of_int" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rti");
                            self.line(&format!(
                                "MakoString {tmp} = mako_reflect_type_of_int({v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "reflect_type_of_string" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rts");
                            self.line(&format!(
                                "MakoString {tmp} = mako_reflect_type_of_string({v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "reflect_kind_of_int" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rki");
                            self.line(&format!(
                                "MakoString {tmp} = mako_reflect_kind_of_int({v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "reflect_kind_of_string" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rks");
                            self.line(&format!(
                                "MakoString {tmp} = mako_reflect_kind_of_string({v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "reflect_value_string_int" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rvi");
                            self.line(&format!(
                                "MakoString {tmp} = mako_reflect_value_string_int({v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "reflect_value_string_str" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rvs");
                            self.line(&format!(
                                "MakoString {tmp} = mako_reflect_value_string_str({v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "reflect_len_string" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_reflect_len_string({v})"));
                        }
                        "httptest_serve_once" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_httptest_serve_once({p}, {b})"),
                            );
                        }
                        "httptest_get" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("htg");
                            self.line(&format!("MakoString {tmp} = mako_httptest_get({u});"));
                            return ("MakoString".into(), tmp);
                        }
                        "httptest_status" => {
                            return ("int64_t".into(), "mako_httptest_status()".into());
                        }
                        "httptest_header" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hth");
                            self.line(&format!("MakoString {tmp} = mako_httptest_header({n});"));
                            return ("MakoString".into(), tmp);
                        }
                        "aead_available" => {
                            return ("int64_t".into(), "mako_aead_available()".into());
                        }
                        "argon2_available" => {
                            return ("int64_t".into(), "mako_argon2_available()".into());
                        }
                        "argon2id_hash" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("a2h");
                            self.line(&format!("MakoString {tmp} = mako_argon2id_hash({p});"));
                            return ("MakoString".into(), tmp);
                        }
                        "argon2id_verify" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("a2v");
                            self.line(&format!(
                                "int64_t {tmp} = mako_argon2id_verify({h}, {p});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "bcrypt_hash" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, c) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("bch");
                            self.line(&format!(
                                "MakoString {tmp} = mako_bcrypt_hash({p}, {c});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "bcrypt_verify" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("bcv");
                            self.line(&format!(
                                "int64_t {tmp} = mako_bcrypt_verify({h}, {p});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "bcrypt_available" => {
                            let tmp = self.fresh("bca");
                            self.line(&format!(
                                "int64_t {tmp} = mako_bcrypt_available();"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "aes_gcm_seal" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, p) = self.emit_expr(&args[2]);
                            let (_, a) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("ags");
                            self.line(&format!(
                                "MakoString {tmp} = mako_aes_gcm_seal({k}, {n}, {p}, {a});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "aes_gcm_open" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, s) = self.emit_expr(&args[2]);
                            let (_, a) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("ago");
                            self.line(&format!(
                                "MakoString {tmp} = mako_aes_gcm_open({k}, {n}, {s}, {a});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "chacha20_poly1305_seal" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, p) = self.emit_expr(&args[2]);
                            let (_, a) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("ccs");
                            self.line(&format!(
                                "MakoString {tmp} = mako_chacha20_poly1305_seal({k}, {n}, {p}, {a});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "chacha20_poly1305_open" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, s) = self.emit_expr(&args[2]);
                            let (_, a) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("cco");
                            self.line(&format!(
                                "MakoString {tmp} = mako_chacha20_poly1305_open({k}, {n}, {s}, {a});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "multipart_boundary" => {
                            let (_, ct) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("mpb");
                            self.line(&format!(
                                "MakoString {tmp} = mako_multipart_boundary({ct});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "multipart_form_value" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, bd) = self.emit_expr(&args[1]);
                            let (_, n) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("mpv");
                            self.line(&format!(
                                "MakoString {tmp} = mako_multipart_form_value({b}, {bd}, {n});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "multipart_file_name" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, bd) = self.emit_expr(&args[1]);
                            let (_, n) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("mpfn");
                            self.line(&format!(
                                "MakoString {tmp} = mako_multipart_file_name({b}, {bd}, {n});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "multipart_file_content_type" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, bd) = self.emit_expr(&args[1]);
                            let (_, n) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("mpct");
                            self.line(&format!(
                                "MakoString {tmp} = mako_multipart_file_content_type({b}, {bd}, {n});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "multipart_file_value" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, bd) = self.emit_expr(&args[1]);
                            let (_, n) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("mpfv");
                            self.line(&format!(
                                "MakoString {tmp} = mako_multipart_file_value({b}, {bd}, {n});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "multipart_file_size" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, bd) = self.emit_expr(&args[1]);
                            let (_, n) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_multipart_file_size({b}, {bd}, {n})"),
                            );
                        }
                        "multipart_file_allowed" => {
                            let (_, b) = self.emit_expr(&args[0]);
                            let (_, bd) = self.emit_expr(&args[1]);
                            let (_, n) = self.emit_expr(&args[2]);
                            let (_, max) = self.emit_expr(&args[3]);
                            let (_, types) = self.emit_expr(&args[4]);
                            return (
                                "int64_t".into(),
                                format!(
                                    "mako_multipart_file_allowed({b}, {bd}, {n}, {max}, {types})"
                                ),
                            );
                        }
                        "regex_find_all" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let (_, lim) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("rfa");
                            self.line(&format!(
                                "MakoStrArray {tmp} = mako_regex_find_all({p}, {t}, {lim});"
                            ));
                            return ("MakoStrArray".into(), tmp);
                        }
                        "regex_replace" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let (_, r) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("rr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_regex_replace({p}, {t}, {r});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "regex_replace_all" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let (_, r) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("rra");
                            self.line(&format!(
                                "MakoString {tmp} = mako_regex_replace_all({p}, {t}, {r});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "zip_deflate_available" => {
                            return ("int64_t".into(), "mako_zip_deflate_available()".into());
                        }
                        "gif_encode_rgb" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            let (_, h) = self.emit_expr(&args[1]);
                            let (_, px) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("gif");
                            self.line(&format!(
                                "MakoString {tmp} = mako_gif_encode_rgb({w}, {h}, {px});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "gif_decode_rgb" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gifd");
                            self.line(&format!("MakoString {tmp} = mako_gif_decode_rgb({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "gif_width" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_gif_width({s})"));
                        }
                        "gif_height" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_gif_height({s})"));
                        }
                        "jpeg_encode_gray" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            let (_, h) = self.emit_expr(&args[1]);
                            let (_, px) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("jpg");
                            self.line(&format!(
                                "MakoString {tmp} = mako_jpeg_encode_gray({w}, {h}, {px});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "jpeg_decode_gray" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("jpgd");
                            self.line(&format!("MakoString {tmp} = mako_jpeg_decode_gray({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "jpeg_width" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_jpeg_width({s})"));
                        }
                        "jpeg_height" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_jpeg_height({s})"));
                        }
                        "reflect_struct_num_fields" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_reflect_struct_num_fields({s})"),
                            );
                        }
                        "reflect_struct_field_name" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("rsn");
                            self.line(&format!(
                                "MakoString {tmp} = mako_reflect_struct_field_name({s}, {i});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "reflect_struct_field_type" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("rst");
                            self.line(&format!(
                                "MakoString {tmp} = mako_reflect_struct_field_type({s}, {i});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "reflect_struct_has_field" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_reflect_struct_has_field({s}, {n})"),
                            );
                        }
                        "html_template_execute" => {
                            let (_, tmpl) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let (_, v) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("hte");
                            self.line(&format!(
                                "MakoString {tmp} = mako_html_template_execute({tmpl}, {k}, {v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "gob_encode_string" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ges");
                            self.line(&format!("MakoString {tmp} = mako_gob_encode_string({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "gob_decode_string" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gds");
                            self.line(&format!("MakoString {tmp} = mako_gob_decode_string({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "gob_encode_int" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gei");
                            self.line(&format!("MakoString {tmp} = mako_gob_encode_int({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "gob_decode_int" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_gob_decode_int({s})"));
                        }
                        "mail_parse_address" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("mpa");
                            self.line(&format!("MakoString {tmp} = mako_mail_parse_address({a});"));
                            return ("MakoString".into(), tmp);
                        }
                        "mail_header_get" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("mhg");
                            self.line(&format!(
                                "MakoString {tmp} = mako_mail_header_get({m}, {n});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "mail_address_ok" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_mail_address_ok({a})"));
                        }
                        "slog_set_level" => {
                            let (_, l) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_slog_set_level({l});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "slog_info" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_slog_info({m});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "slog_warn" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_slog_warn({m});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "slog_error" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_slog_error({m});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "slog_debug" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_slog_debug({m});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "slog_with" => {
                            let (_, l) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            let (_, k) = self.emit_expr(&args[2]);
                            let (_, v) = self.emit_expr(&args[3]);
                            self.line(&format!("mako_slog_with({l}, {m}, {k}, {v});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "regex_valid" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_regex_valid({p})"));
                        }
                        "regex_quote_meta" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rqm");
                            self.line(&format!("MakoString {tmp} = mako_regex_quote_meta({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "zip_create" => {
                            let tmp = self.fresh("zw");
                            self.line(&format!("MakoZipWriter *{tmp} = mako_zip_create();"));
                            return ("MakoZipWriter*".into(), tmp);
                        }
                        "zip_add" => {
                            let (_, z) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, d) = self.emit_expr(&args[2]);
                            return ("int64_t".into(), format!("mako_zip_add({z}, {n}, {d})"));
                        }
                        "zip_write_to" => {
                            let (_, z) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_zip_write_to({z}, {p})"));
                        }
                        "zip_close" => {
                            let (_, z) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_zip_close({z});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "zip_list" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("zl");
                            self.line(&format!("MakoStrArray {tmp} = mako_zip_list({p});"));
                            return ("MakoStrArray".into(), tmp);
                        }
                        "html_template_execute2" => {
                            let (_, t) = self.emit_expr(&args[0]);
                            let (_, k1) = self.emit_expr(&args[1]);
                            let (_, v1) = self.emit_expr(&args[2]);
                            let (_, k2) = self.emit_expr(&args[3]);
                            let (_, v2) = self.emit_expr(&args[4]);
                            let tmp = self.fresh("ht2");
                            self.line(&format!("MakoString {tmp} = mako_html_template_execute2({t}, {k1}, {v1}, {k2}, {v2});"));
                            return ("MakoString".into(), tmp);
                        }
                        "html_template_execute3" => {
                            let (_, t) = self.emit_expr(&args[0]);
                            let (_, k1) = self.emit_expr(&args[1]);
                            let (_, v1) = self.emit_expr(&args[2]);
                            let (_, k2) = self.emit_expr(&args[3]);
                            let (_, v2) = self.emit_expr(&args[4]);
                            let (_, k3) = self.emit_expr(&args[5]);
                            let (_, v3) = self.emit_expr(&args[6]);
                            let tmp = self.fresh("ht3");
                            self.line(&format!("MakoString {tmp} = mako_html_template_execute3({t}, {k1}, {v1}, {k2}, {v2}, {k3}, {v3});"));
                            return ("MakoString".into(), tmp);
                        }
                        "html_template_if" => {
                            let (_, t) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let (_, cnd) = self.emit_expr(&args[2]);
                            let (_, v) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("hti");
                            self.line(&format!(
                                "MakoString {tmp} = mako_html_template_if({t}, {k}, {cnd}, {v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "html_template_range" => {
                            let (_, t) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let (_, csv) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("htr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_html_template_range({t}, {k}, {csv});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "html_template_with" => {
                            let (_, t) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let (_, v) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("htw");
                            self.line(&format!(
                                "MakoString {tmp} = mako_html_template_with({t}, {k}, {v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "gob_encode_map_ss" => {
                            let (_, m) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gem");
                            self.line(&format!("MakoString {tmp} = mako_gob_encode_map_ss({m});"));
                            return ("MakoString".into(), tmp);
                        }
                        "gob_decode_map_ss" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gdm");
                            self.line(&format!("MakoMapSS *{tmp} = mako_gob_decode_map_ss({s});"));
                            return ("MakoMapSS*".into(), tmp);
                        }
                        "gob_encode_struct" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ges");
                            self.line(&format!("MakoString {tmp} = mako_gob_encode_struct({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "gob_decode_struct" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gds");
                            self.line(&format!(
                                "MakoReflectValue *{tmp} = mako_gob_decode_struct({s});"
                            ));
                            return ("MakoReflectValue*".into(), tmp);
                        }
                        "binary_put_u16le" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("b16");
                            self.line(&format!("MakoString {tmp} = mako_binary_put_u16le({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "binary_put_u32le" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("b32");
                            self.line(&format!("MakoString {tmp} = mako_binary_put_u32le({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "binary_put_u64le" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("b64");
                            self.line(&format!("MakoString {tmp} = mako_binary_put_u64le({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "binary_u16le" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_binary_u16le({s})"));
                        }
                        "binary_u32le" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_binary_u32le({s})"));
                        }
                        "binary_u64le" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_binary_u64le({s})"));
                        }
                        "binary_put_u16be" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("bbe16");
                            self.line(&format!("MakoString {tmp} = mako_binary_put_u16be({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "binary_put_u32be" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("bbe32");
                            self.line(&format!("MakoString {tmp} = mako_binary_put_u32be({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "binary_put_u64be" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("bbe64");
                            self.line(&format!("MakoString {tmp} = mako_binary_put_u64be({v});"));
                            return ("MakoString".into(), tmp);
                        }
                        "binary_u16be" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_binary_u16be({s})"));
                        }
                        "binary_u32be" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_binary_u32be({s})"));
                        }
                        "binary_u64be" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_binary_u64be({s})"));
                        }
                        "smtp_format_message" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let (_, s) = self.emit_expr(&args[2]);
                            let (_, b) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("sm");
                            self.line(&format!(
                                "MakoString {tmp} = mako_smtp_format_message({f}, {t}, {s}, {b});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "smtp_send_soft" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, m) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_smtp_send_soft({h}, {p}, {m})"),
                            );
                        }
                        "smtp_send_dialog" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, f) = self.emit_expr(&args[2]);
                            let (_, t) = self.emit_expr(&args[3]);
                            let (_, m) = self.emit_expr(&args[4]);
                            return (
                                "int64_t".into(),
                                format!("mako_smtp_send_dialog({h}, {p}, {f}, {t}, {m})"),
                            );
                        }
                        "reflect_value_new" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rv");
                            self.line(&format!(
                                "MakoReflectValue *{tmp} = mako_reflect_value_new({s});"
                            ));
                            return ("MakoReflectValue*".into(), tmp);
                        }
                        "reflect_value_from_2" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, a) = self.emit_expr(&args[1]);
                            let (_, b) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("rv2");
                            self.line(&format!(
                                "MakoReflectValue *{tmp} = mako_reflect_value_from_2({s}, {a}, {b});"
                            ));
                            return ("MakoReflectValue*".into(), tmp);
                        }
                        "reflect_value_from_2_int" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, a) = self.emit_expr(&args[1]);
                            let (_, b) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("rvi");
                            self.line(&format!(
                                "MakoReflectValue *{tmp} = mako_reflect_value_from_2_int({s}, {a}, {b});"
                            ));
                            return ("MakoReflectValue*".into(), tmp);
                        }
                        "reflect_value_of" => {
                            // POD / nested-POD struct → bag: leaf fields as string via set_at.
                            let (vty, v) = self.emit_expr(&args[0]);
                            let fields_opt = self
                                .structs
                                .get(&vty)
                                .or_else(|| self.structs.values().find(|s| s.c_name == vty))
                                .map(|info| (info.c_name.clone(), info.fields.clone()));
                            let tmp = self.fresh("rvo");
                            if let Some((sn, fields)) = fields_opt {
                                if !fields.is_empty() && self.fields_are_reflect_pod(&fields) {
                                    self.line(&format!(
                                        "MakoReflectValue *{tmp} = mako_reflect_value_new(mako_str_from_cstr(\"{sn}\"));"
                                    ));
                                    let mut idx = 0usize;
                                    self.emit_reflect_pod_fields(&tmp, &v, &fields, &mut idx);
                                    return ("MakoReflectValue*".into(), tmp);
                                }
                            }
                            self.line(&format!(
                                "MakoReflectValue *{tmp} = mako_reflect_value_new(mako_str_from_cstr(\"{vty}\"));"
                            ));
                            return ("MakoReflectValue*".into(), tmp);
                        }
                        "reflect_value_set" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let (_, f) = self.emit_expr(&args[1]);
                            let (_, val) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_reflect_value_set({v}, {f}, {val})"),
                            );
                        }
                        "reflect_value_get" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let (_, f) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("rvg");
                            self.line(&format!(
                                "MakoString {tmp} = mako_reflect_value_get({v}, {f});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "reflect_value_num_fields" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_reflect_value_num_fields({v})"),
                            );
                        }
                        "reflect_value_field_at" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("rvfa");
                            self.line(&format!(
                                "MakoString {tmp} = mako_reflect_value_field_at({v}, {i});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "reflect_value_set_at" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            let (_, val) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_reflect_value_set_at({v}, {i}, {val})"),
                            );
                        }
                        "reflect_value_schema" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rvs");
                            self.line(&format!(
                                "MakoString {tmp} = mako_reflect_value_schema({v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "jpeg_encode_gray_dct" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            let (_, h) = self.emit_expr(&args[1]);
                            let (_, px) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("jdct");
                            self.line(&format!(
                                "MakoString {tmp} = mako_jpeg_encode_gray_dct({w}, {h}, {px});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "jpeg_dct_dc" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_jpeg_dct_dc({s})"));
                        }
                        "gif_encode_rgb_lzw" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            let (_, h) = self.emit_expr(&args[1]);
                            let (_, px) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("glzw");
                            self.line(&format!(
                                "MakoString {tmp} = mako_gif_encode_rgb_lzw({w}, {h}, {px});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "gif_decode_rgb_lzw" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gdlzw");
                            self.line(&format!("MakoString {tmp} = mako_gif_decode_rgb_lzw({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "html_template_nested" => {
                            let (_, t) = self.emit_expr(&args[0]);
                            let (_, ok) = self.emit_expr(&args[1]);
                            let (_, oc) = self.emit_expr(&args[2]);
                            let (_, ik) = self.emit_expr(&args[3]);
                            let (_, iv) = self.emit_expr(&args[4]);
                            let tmp = self.fresh("htn");
                            self.line(&format!("MakoString {tmp} = mako_html_template_nested({t}, {ok}, {oc}, {ik}, {iv});"));
                            return ("MakoString".into(), tmp);
                        }
                        "gob_encode_strs" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ges");
                            self.line(&format!("MakoString {tmp} = mako_gob_encode_strs({a});"));
                            return ("MakoString".into(), tmp);
                        }
                        "gob_decode_strs" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gds");
                            self.line(&format!("MakoStrArray {tmp} = mako_gob_decode_strs({s});"));
                            return ("MakoStrArray".into(), tmp);
                        }
                        "smtp_auth_plain" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("sap");
                            self.line(&format!(
                                "MakoString {tmp} = mako_smtp_auth_plain({u}, {p});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "smtp_send_auth" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, u) = self.emit_expr(&args[2]);
                            let (_, pw) = self.emit_expr(&args[3]);
                            let (_, f) = self.emit_expr(&args[4]);
                            let (_, t) = self.emit_expr(&args[5]);
                            let (_, m) = self.emit_expr(&args[6]);
                            return (
                                "int64_t".into(),
                                format!("mako_smtp_send_auth({h}, {p}, {u}, {pw}, {f}, {t}, {m})"),
                            );
                        }
                        "smtp_starttls_available" => {
                            return ("int64_t".into(), "mako_smtp_starttls_available()".into());
                        }
                        "reflect_value_clone" => {
                            let (_, v) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rvc");
                            self.line(&format!(
                                "MakoReflectValue *{tmp} = mako_reflect_value_clone({v});"
                            ));
                            return ("MakoReflectValue*".into(), tmp);
                        }
                        "reflect_value_equal" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_reflect_value_equal({a}, {b})"),
                            );
                        }
                        "jpeg_encode_gray_huff" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            let (_, h) = self.emit_expr(&args[1]);
                            let (_, px) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("jhuff");
                            self.line(&format!(
                                "MakoString {tmp} = mako_jpeg_encode_gray_huff({w}, {h}, {px});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "jpeg_huff_block" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("jhb");
                            self.line(&format!("MakoString {tmp} = mako_jpeg_huff_block({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "jpeg_encode_gray_jfif" => {
                            let (_, w) = self.emit_expr(&args[0]);
                            let (_, h) = self.emit_expr(&args[1]);
                            let (_, px) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("jjfif");
                            self.line(&format!(
                                "MakoString {tmp} = mako_jpeg_encode_gray_jfif({w}, {h}, {px});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "jpeg_is_jfif" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_jpeg_is_jfif({s})"));
                        }
                        "smtp_send_starttls" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, u) = self.emit_expr(&args[2]);
                            let (_, pw) = self.emit_expr(&args[3]);
                            let (_, f) = self.emit_expr(&args[4]);
                            let (_, t) = self.emit_expr(&args[5]);
                            let (_, m) = self.emit_expr(&args[6]);
                            return (
                                "int64_t".into(),
                                format!(
                                    "mako_smtp_send_starttls({h}, {p}, {u}, {pw}, {f}, {t}, {m})"
                                ),
                            );
                        }
                        "reflect_type_schema" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rts");
                            self.line(&format!(
                                "MakoString {tmp} = mako_reflect_type_schema({n});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "reflect_value_of_type" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rvot");
                            self.line(&format!(
                                "MakoReflectValue *{tmp} = mako_reflect_value_of_type({n});"
                            ));
                            return ("MakoReflectValue*".into(), tmp);
                        }
                        "reflect_type_count" => {
                            return ("int64_t".into(), "mako_reflect_type_count()".into());
                        }
                        "reflect_type_name_at" => {
                            let (_, i) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rtna");
                            self.line(&format!(
                                "MakoString {tmp} = mako_reflect_type_name_at({i});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "str_cut" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, sep) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("scut");
                            self.line(&format!("MakoStrArray {tmp} = mako_str_cut({s}, {sep});"));
                            return ("MakoStrArray".into(), tmp);
                        }
                        "str_count" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, sub) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_str_count({s}, {sub})"));
                        }

                        "safe_add" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_safe_add({a}, {b})"));
                        }
                        "dlopen_probe" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_dlopen_probe({p})"));
                        }
                        "http_listen" => {
                            let (_, port) = self.emit_expr(&args[0]);
                            let (_, body) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("hl");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http_listen({port}, {body});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_listen_stub" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_tls_listen_stub({p})"));
                        }
                        "ws_echo_stub" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ws");
                            self.line(&format!("int64_t {tmp} = mako_ws_echo_stub({p});"));
                            return ("int64_t".into(), tmp);
                        }
                        "ws_echo_once" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("wso");
                            self.line(&format!("int64_t {tmp} = mako_ws_echo_once({p});"));
                            return ("int64_t".into(), tmp);
                        }
                        "ws_echo" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("wse");
                            self.line(&format!("int64_t {tmp} = mako_ws_echo({p});"));
                            return ("int64_t".into(), tmp);
                        }
                        "ws_accept_key" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("wsak");
                            self.line(&format!("MakoString {tmp} = mako_ws_accept_key({k});"));
                            return ("MakoString".into(), tmp);
                        }
                        "ws_upgrade_request_ok" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_ws_upgrade_request_ok({r})"));
                        }
                        "ws_client_request" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, k) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("wsreq");
                            self.line(&format!(
                                "MakoString {tmp} = mako_ws_client_request({h}, {p}, {k});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "ws_client_accept_ok" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, r) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_ws_client_accept_ok({k}, {r})"),
                            );
                        }
                        "ws_accept" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_ws_accept({f})"));
                        }
                        "ws_recv" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, max) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("wsr");
                            self.line(&format!("MakoString {tmp} = mako_ws_recv({f}, {max});"));
                            return ("MakoString".into(), tmp);
                        }
                        "ws_last_opcode" => {
                            return ("int64_t".into(), "mako_ws_last_frame_opcode()".into());
                        }
                        "ws_send_text" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_ws_send_text_msg({f}, {m})"));
                        }
                        "ws_send_binary" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_ws_send_binary_msg({f}, {m})"),
                            );
                        }
                        "ws_send_ping" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_ws_send_ping_msg({f}, {m})"));
                        }
                        "ws_send_close" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, c) = self.emit_expr(&args[1]);
                            let (_, r) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_ws_send_close({f}, {c}, {r})"),
                            );
                        }
                        "ws_close" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_ws_close({f})"));
                        }
                        "ws_client_connect" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let (_, k) = self.emit_expr(&args[3]);
                            return (
                                "int64_t".into(),
                                format!("mako_ws_client_connect({h}, {p}, {path}, {k})"),
                            );
                        }
                        "ws_client_send_text" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_ws_client_send_text({f}, {m})"),
                            );
                        }
                        "ws_client_send_binary" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_ws_client_send_binary({f}, {m})"),
                            );
                        }
                        "ws_client_send_ping" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_ws_client_send_ping({f}, {m})"),
                            );
                        }
                        "http_header" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("hh");
                            self.line(&format!("MakoString {tmp} = mako_http_header({c}, {n});"));
                            return ("MakoString".into(), tmp);
                        }
                        "http_next" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_http_next({c})"));
                        }
                        "http_keepalive" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_http_keepalive({c})"));
                        }
                        "http_close" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_http_close({c})"));
                        }
                        "http_close_listener" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_http_close_listener({f})"));
                        }
                        "http_shutdown_begin" => {
                            let (_, grace_ms) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_http_shutdown_begin({grace_ms})"),
                            );
                        }
                        "http_shutdown_reset" => {
                            return ("int64_t".into(), "mako_http_shutdown_reset()".into());
                        }
                        "http_shutdown_requested" => {
                            return ("int64_t".into(), "mako_http_shutdown_requested()".into());
                        }
                        "http_shutdown_ready" => {
                            return ("int64_t".into(), "mako_http_shutdown_ready()".into());
                        }
                        "http_shutdown_deadline" => {
                            return ("int64_t".into(), "mako_http_shutdown_deadline()".into());
                        }
                        "http_shutdown_remaining" => {
                            return ("int64_t".into(), "mako_http_shutdown_remaining()".into());
                        }
                        "http_shutdown_expired" => {
                            return ("int64_t".into(), "mako_http_shutdown_expired()".into());
                        }
                        "http_active_connections" => {
                            return ("int64_t".into(), "mako_http_active_connections()".into());
                        }
                        "http_shutdown_drain_conn" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_http_shutdown_drain_conn({c})"),
                            );
                        }
                        "http_shutdown_from_signal" => {
                            let (_, sig) = self.emit_expr(&args[0]);
                            let (_, grace_ms) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_http_shutdown_from_signal({sig}, {grace_ms})"),
                            );
                        }
                        "http_respond_ct" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, st) = self.emit_expr(&args[1]);
                            let (_, ct) = self.emit_expr(&args[2]);
                            let (_, b) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("hrc");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http_respond_ct({c}, {st}, {ct}, {b});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "http_respond_json" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, st) = self.emit_expr(&args[1]);
                            let (_, b) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("hrj");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http_respond_json({c}, {st}, {b});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_get_insecure" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("tg");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_get_insecure({h}, {p}, {path});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_server_available" => {
                            return ("int64_t".into(), "mako_tls_server_available()".into());
                        }
                        "tls_server_new" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tsv");
                            self.line(&format!("void *{tmp} = mako_tls_server_new({c}, {k});"));
                            return ("void*".into(), tmp);
                        }
                        "tls_server_new_tls13" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tsv13");
                            self.line(&format!(
                                "void *{tmp} = mako_tls_server_new_tls13({c}, {k});"
                            ));
                            return ("void*".into(), tmp);
                        }
                        "tls_accept" => {
                            let (_, ctx) = self.emit_expr(&args[0]);
                            let (_, fd) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tac");
                            self.line(&format!("void *{tmp} = mako_tls_accept({ctx}, {fd});"));
                            return ("void*".into(), tmp);
                        }
                        "tls_accept_start" => {
                            let (_, ctx) = self.emit_expr(&args[0]);
                            let (_, fd) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("tas");
                            self.line(&format!(
                                "void *{tmp} = mako_tls_accept_start({ctx}, {fd});"
                            ));
                            return ("void*".into(), tmp);
                        }
                        "tls_handshake_step" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_tls_handshake_step({c})"),
                            );
                        }
                        "tls_is_init_finished" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_tls_is_init_finished({c})"),
                            );
                        }
                        "tls_want_read" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_tls_want_read({c})"));
                        }
                        "tls_want_write" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_tls_want_write({c})"));
                        }
                        "tls_conn_fd" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_tls_conn_fd({c})"));
                        }
                        "tls_read_nb" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("trnb");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_read_nb({c}, {n});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_write_nb" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_tls_write_nb({c}, {d})"),
                            );
                        }
                        "tls_read" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("trd");
                            self.line(&format!("MakoString {tmp} = mako_tls_read({c}, {n});"));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_write" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("twr");
                            self.line(&format!("int64_t {tmp} = mako_tls_write({c}, {d});"));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_conn_alpn" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("tap");
                            self.line(&format!("MakoString {tmp} = mako_tls_conn_alpn({c});"));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_conn_close" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_tls_conn_close({c});"));
                            return ("int64_t".into(), "0".into());
                        }
                        "tls_server_free" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_tls_server_free({c});"));
                            return ("int64_t".into(), "0".into());
                        }
                        "tls_get" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let (_, ca) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("tgv");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_get({h}, {p}, {path}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_post" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let (_, body) = self.emit_expr(&args[3]);
                            let (_, ca) = self.emit_expr(&args[4]);
                            let tmp = self.fresh("tp");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_post({h}, {p}, {path}, {body}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_handshake_ok" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, ca) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("tho");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_handshake_ok({h}, {p}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_handshake_version" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, ca) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("thv");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_handshake_version({h}, {p}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_h2_settings_exchange" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, ca) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("th2");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_h2_settings_exchange({h}, {p}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_h2_get" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let (_, ca) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("th2g");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_h2_get({h}, {p}, {path}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_h2_post" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let (_, body) = self.emit_expr(&args[3]);
                            let (_, ca) = self.emit_expr(&args[4]);
                            let tmp = self.fresh("th2p");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_h2_post({h}, {p}, {path}, {body}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_h2_get_twice" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let (_, ca) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("th2t");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_h2_get_twice({h}, {p}, {path}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_h2_mux" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, ca) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("th2m");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_h2_mux({h}, {p}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_h2_window_get" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let (_, ca) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("th2w");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_h2_window_get({h}, {p}, {path}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "nghttp2_available" => {
                            return ("int64_t".into(), "mako_nghttp2_available()".into());
                        }
                        "quiche_available" => {
                            return ("int64_t".into(), "mako_quiche_available()".into());
                        }
                        "quiche_version" => {
                            let tmp = self.fresh("qv");
                            self.line(&format!("MakoString {tmp} = mako_quiche_version();"));
                            return ("MakoString".into(), tmp);
                        }
                        "quiche_handshake" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, sni) = self.emit_expr(&args[2]);
                            let (_, ver) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("qhs");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quiche_handshake({h}, {p}, {sni}, {ver});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quiche_h3_get" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let (_, sni) = self.emit_expr(&args[3]);
                            let (_, ver) = self.emit_expr(&args[4]);
                            let tmp = self.fresh("qh3");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quiche_h3_get({h}, {p}, {path}, {sni}, {ver});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quiche_h3_post" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let (_, body) = self.emit_expr(&args[3]);
                            let (_, sni) = self.emit_expr(&args[4]);
                            let (_, ver) = self.emit_expr(&args[5]);
                            let tmp = self.fresh("qh3p");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quiche_h3_post({h}, {p}, {path}, {body}, {sni}, {ver});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quiche_h3_get_two" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path1) = self.emit_expr(&args[2]);
                            let (_, path2) = self.emit_expr(&args[3]);
                            let (_, sni) = self.emit_expr(&args[4]);
                            let (_, ver) = self.emit_expr(&args[5]);
                            let tmp = self.fresh("qh3m");
                            self.line(&format!(
                                "MakoString {tmp} = mako_quiche_h3_get_two({h}, {p}, {path1}, {path2}, {sni}, {ver});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "quiche_start_server" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, cert) = self.emit_expr(&args[1]);
                            let (_, key) = self.emit_expr(&args[2]);
                            let (_, root) = self.emit_expr(&args[3]);
                            let (_, name) = self.emit_expr(&args[4]);
                            let tmp = self.fresh("qss");
                            self.line(&format!(
                                "int64_t {tmp} = mako_quiche_start_server({p}, {cert}, {key}, {root}, {name});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "quiche_stop_server" => {
                            let (_, pid) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("qstop");
                            self.line(&format!("int64_t {tmp} = mako_quiche_stop_server({pid});"));
                            return ("int64_t".into(), tmp);
                        }
                        "h3_server_available" => {
                            return ("int64_t".into(), "mako_h3_server_available()".into());
                        }
                        "h3_server_new" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_h3_server_new({c}, {k})"),
                            );
                        }
                        "h3_server_bind" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, host) = self.emit_expr(&args[1]);
                            let (_, p) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_h3_server_bind({h}, {host}, {p})"),
                            );
                        }
                        "h3_server_fd" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_h3_server_fd({h})"));
                        }
                        "h3_server_poll" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, ms) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_h3_server_poll({h}, {ms})"),
                            );
                        }
                        "h3_accept_stream" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_h3_accept_stream({h})"));
                        }
                        "h3_stream_read" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("h3r");
                            self.line(&format!(
                                "MakoString {tmp} = mako_h3_stream_read({h}, {s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "h3_stream_write" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            let (_, d) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_h3_stream_write({h}, {s}, {d})"),
                            );
                        }
                        "h3_server_close" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_h3_server_close({h})"));
                        }
                        "nghttp2_get" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let (_, ca) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("ngg");
                            self.line(&format!(
                                "MakoString {tmp} = mako_nghttp2_get({h}, {p}, {path}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "nghttp2_post" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let (_, body) = self.emit_expr(&args[3]);
                            let (_, ca) = self.emit_expr(&args[4]);
                            let tmp = self.fresh("ngp");
                            self.line(&format!(
                                "MakoString {tmp} = mako_nghttp2_post({h}, {p}, {path}, {body}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "nghttp2_get_two" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path1) = self.emit_expr(&args[2]);
                            let (_, path2) = self.emit_expr(&args[3]);
                            let (_, ca) = self.emit_expr(&args[4]);
                            let tmp = self.fresh("ng2");
                            self.line(&format!(
                                "MakoString {tmp} = mako_nghttp2_get_two({h}, {p}, {path1}, {path2}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_grpc_unary" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let (_, name) = self.emit_expr(&args[3]);
                            let (_, id) = self.emit_expr(&args[4]);
                            let (_, ca) = self.emit_expr(&args[5]);
                            let tmp = self.fresh("tgu");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_grpc_unary({h}, {p}, {path}, {name}, {id}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "tls_grpc_stream" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let (_, n1) = self.emit_expr(&args[3]);
                            let (_, i1) = self.emit_expr(&args[4]);
                            let (_, n2) = self.emit_expr(&args[5]);
                            let (_, i2) = self.emit_expr(&args[6]);
                            let (_, ca) = self.emit_expr(&args[7]);
                            let tmp = self.fresh("tgs");
                            self.line(&format!(
                                "MakoString {tmp} = mako_tls_grpc_stream({h}, {p}, {path}, {n1}, {i1}, {n2}, {i2}, {ca});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "chan_select2" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, ms) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_chan_select2({a}, {b}, {ms})"),
                            );
                        }
                        "chan_str_select2" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, ms) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_chan_str_select2({a}, {b}, {ms})"),
                            );
                        }
                        "chan_select_value_str" => {
                            let tmp = self.fresh("sels");
                            self.line(&format!(
                                "MakoString {tmp} = mako_chan_select_value_str();"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "chan_select_value_ptr" => {
                            return (
                                "void*".into(),
                                "mako_chan_select_value_ptr()".into(),
                            );
                        }
                        "chan_select3" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, c) = self.emit_expr(&args[2]);
                            let (_, ms) = self.emit_expr(&args[3]);
                            return (
                                "int64_t".into(),
                                format!("mako_chan_select3({a}, {b}, {c}, {ms})"),
                            );
                        }
                        "chan_select4" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, c) = self.emit_expr(&args[2]);
                            let (_, d) = self.emit_expr(&args[3]);
                            let (_, ms) = self.emit_expr(&args[4]);
                            return (
                                "int64_t".into(),
                                format!("mako_chan_select4({a}, {b}, {c}, {d}, {ms})"),
                            );
                        }
                        "sqlite_query_int" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, q) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_sqlite_query_int({p}, {q})"));
                        }
                        "sqlite_query_int_params" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, q) = self.emit_expr(&args[1]);
                            let (_, a) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!(
                                    "mako_sqlite_query_int_params({p}, {q}, {a}.data, (int){a}.len)"
                                ),
                            );
                        }
                        "const_eq" | "crypto_eq" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_const_eq({a}, {b})"));
                        }
                        "secret_from_str" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("sec");
                            self.line(&format!("MakoSecret {tmp} = mako_secret_from_str({s});"));
                            return ("MakoSecret".into(), tmp);
                        }
                        "secret_drop" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            self.line(&format!("mako_secret_drop(&{s});"));
                            return ("void".into(), "/*void*/".into());
                        }
                        "http_header_ok" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("(int64_t)mako_http_header_pair_ok({n}, {v})"),
                            );
                        }
                        "unsafe_index" => {
                            let (_, arr) = self.emit_expr(&args[0]);
                            let (_, idx) = self.emit_expr(&args[1]);
                            // Always unchecked — caller must be in unsafe or accept UB.
                            return ("int64_t".into(), format!("{arr}.data[{idx}]"));
                        }
                        "sqlite_query_text" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, q) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("sq");
                            self.line(&format!(
                                "MakoString {tmp} = mako_sqlite_query_text({p}, {q});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "redis_ping" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("rp");
                            self.line(&format!("MakoString {tmp} = mako_redis_ping({h}, {p});"));
                            return ("MakoString".into(), tmp);
                        }
                        "redis_set" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, k) = self.emit_expr(&args[2]);
                            let (_, v) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("rs");
                            self.line(&format!(
                                "MakoString {tmp} = mako_redis_set({h}, {p}, {k}, {v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "redis_get" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, k) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("rg");
                            self.line(&format!(
                                "MakoString {tmp} = mako_redis_get({h}, {p}, {k});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "redis_del" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, k) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("rdl");
                            self.line(&format!(
                                "MakoString {tmp} = mako_redis_del({h}, {p}, {k});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "redis_exists" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, k) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("rex");
                            self.line(&format!(
                                "MakoString {tmp} = mako_redis_exists({h}, {p}, {k});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "redis_mock_once" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_redis_mock_once({p})"));
                        }
                        "redis_mock_kv" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_redis_mock_kv({p}, {n})"));
                        }
                        "quic_stub" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_quic_stub({p})"));
                        }
                        "pg_connect" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pg");
                            self.line(&format!("MakoPgConn {tmp} = mako_pg_connect({u});"));
                            return ("MakoPgConn".into(), tmp);
                        }
                        "pg_ok" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_pg_ok({c})"));
                        }
                        "pg_close" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_pg_close({c})"));
                        }
                        "pg_exec" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_pg_exec({c}, {s})"));
                        }
                        "pg_exec_row_count" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_pg_exec_row_count({c}, {s})"),
                            );
                        }
                        "pg_connect_url" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pgu");
                            self.line(&format!("MakoString {tmp} = mako_pg_connect_url({u});"));
                            return ("MakoString".into(), tmp);
                        }
                        "mysql_connect" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("my");
                            self.line(&format!("MakoMysqlConn {tmp} = mako_mysql_connect({u});"));
                            return ("MakoMysqlConn".into(), tmp);
                        }
                        "mysql_connect_url" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("myu");
                            self.line(&format!("MakoString {tmp} = mako_mysql_connect_url({u});"));
                            return ("MakoString".into(), tmp);
                        }
                        "mysql_ok" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_mysql_ok({c})"));
                        }
                        "mysql_close" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_mysql_close({c})"));
                        }
                        "mysql_is_mariadb" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_mysql_is_mariadb({u})"));
                        }
                        "mysql_driver_name" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("myd");
                            self.line(&format!("MakoString {tmp} = mako_mysql_driver_name({u});"));
                            return ("MakoString".into(), tmp);
                        }
                        "redis_connect" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rd");
                            self.line(&format!("MakoRedisConn {tmp} = mako_redis_connect({u});"));
                            return ("MakoRedisConn".into(), tmp);
                        }
                        "redis_connect_url" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rdu");
                            self.line(&format!("MakoString {tmp} = mako_redis_connect_url({u});"));
                            return ("MakoString".into(), tmp);
                        }
                        "redis_ok" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_redis_ok({c})"));
                        }
                        "redis_close" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_redis_close({c})"));
                        }
                        "redis_conn_ping" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("rdp");
                            self.line(&format!("MakoString {tmp} = mako_redis_conn_ping({c});"));
                            return ("MakoString".into(), tmp);
                        }
                        "redis_conn_set" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let (_, v) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("rds");
                            self.line(&format!(
                                "MakoString {tmp} = mako_redis_conn_set({c}, {k}, {v});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "redis_conn_get" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("rdg");
                            self.line(&format!(
                                "MakoString {tmp} = mako_redis_conn_get({c}, {k});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "redis_conn_del" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("rdd");
                            self.line(&format!(
                                "MakoString {tmp} = mako_redis_conn_del({c}, {k});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "redis_conn_exists" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("rde");
                            self.line(&format!(
                                "MakoString {tmp} = mako_redis_conn_exists({c}, {k});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "mongo_connect_url" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("mgu");
                            self.line(&format!("MakoString {tmp} = mako_mongo_connect_url({u});"));
                            return ("MakoString".into(), tmp);
                        }
                        "mongo_find_one_request" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, f) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("mgf");
                            self.line(&format!(
                                "MakoString {tmp} = mako_mongo_find_one_request({c}, {f});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "cassandra_connect_url" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("csu");
                            self.line(&format!(
                                "MakoString {tmp} = mako_cassandra_connect_url({u});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "cassandra_select" => {
                            let (_, k) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let (_, w) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("csq");
                            self.line(&format!(
                                "MakoString {tmp} = mako_cassandra_select({k}, {t}, {w});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "clickhouse_connect_url" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("chu");
                            self.line(&format!(
                                "MakoString {tmp} = mako_clickhouse_connect_url({u});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "clickhouse_select" => {
                            let (_, d) = self.emit_expr(&args[0]);
                            let (_, t) = self.emit_expr(&args[1]);
                            let (_, s) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("chq");
                            self.line(&format!(
                                "MakoString {tmp} = mako_clickhouse_select({d}, {t}, {s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "elastic_connect_url" => {
                            let (_, u) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("esu");
                            self.line(&format!(
                                "MakoString {tmp} = mako_elastic_connect_url({u});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "elastic_search_request" => {
                            let (_, i) = self.emit_expr(&args[0]);
                            let (_, q) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("esq");
                            self.line(&format!(
                                "MakoString {tmp} = mako_elastic_search_request({i}, {q});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_encode_message" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("gre");
                            self.line(&format!(
                                "MakoString {tmp} = mako_grpc_encode_message({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_message_len" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("grl");
                            self.line(&format!("int64_t {tmp} = mako_grpc_message_len({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "grpc_message_within_limit" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("gmw");
                            self.line(&format!(
                                "int64_t {tmp} = mako_grpc_message_within_limit({s}, {m});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "grpc_default_max_message" => {
                            let tmp = self.fresh("gdm");
                            self.line(&format!("int64_t {tmp} = mako_grpc_default_max_message();"));
                            return ("int64_t".into(), tmp);
                        }
                        "grpc_message_payload" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("grp");
                            self.line(&format!(
                                "MakoString {tmp} = mako_grpc_message_payload({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_unary_request" => {
                            let (_, n) = self.emit_expr(&args[0]);
                            let (_, i) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("gru");
                            self.line(&format!(
                                "MakoString {tmp} = mako_grpc_unary_request({n}, {i});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_unary_name" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("grun");
                            self.line(&format!("MakoString {tmp} = mako_grpc_unary_name({s});"));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_unary_id" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("grui");
                            self.line(&format!("int64_t {tmp} = mako_grpc_unary_id({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "grpc_content_type" => {
                            let tmp = self.fresh("grct");
                            self.line(&format!("MakoString {tmp} = mako_grpc_content_type();"));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_status_trailer" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("grst");
                            self.line(&format!(
                                "MakoString {tmp} = mako_grpc_status_trailer({c});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_status_code" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("grsc");
                            self.line(&format!("int64_t {tmp} = mako_grpc_status_code({s});"));
                            return ("int64_t".into(), tmp);
                        }
                        "grpc_http2_unary" => {
                            let (_, st) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, i) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("grhu");
                            self.line(&format!(
                                "MakoString {tmp} = mako_grpc_http2_unary({st}, {n}, {i});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_http2_unary_payload" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("grhp");
                            self.line(&format!(
                                "MakoString {tmp} = mako_grpc_http2_unary_payload({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_http2_unary_response" => {
                            let (_, st) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, i) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("ghr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_grpc_http2_unary_response({st}, {n}, {i});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_http2_unary_response_status" => {
                            let (_, st) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, i) = self.emit_expr(&args[2]);
                            let (_, c) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("ghrs");
                            self.line(&format!(
                                "MakoString {tmp} = mako_grpc_http2_unary_response_status({st}, {n}, {i}, {c});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_http2_response_payload" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("grp");
                            self.line(&format!(
                                "MakoString {tmp} = mako_grpc_http2_response_payload({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_http2_response_status" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("grs");
                            self.line(&format!(
                                "int64_t {tmp} = mako_grpc_http2_response_status({s});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "grpc_http2_stream_data" => {
                            let (_, st) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let (_, i) = self.emit_expr(&args[2]);
                            let (_, e) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("ghsd");
                            self.line(&format!(
                                "MakoString {tmp} = mako_grpc_http2_stream_data({st}, {n}, {i}, {e});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_http2_stream_two" => {
                            let (_, st) = self.emit_expr(&args[0]);
                            let (_, n1) = self.emit_expr(&args[1]);
                            let (_, i1) = self.emit_expr(&args[2]);
                            let (_, n2) = self.emit_expr(&args[3]);
                            let (_, i2) = self.emit_expr(&args[4]);
                            let tmp = self.fresh("ghst");
                            self.line(&format!(
                                "MakoString {tmp} = mako_grpc_http2_stream_two({st}, {n1}, {i1}, {n2}, {i2});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_http2_stream_data_count" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, st) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ghsc");
                            self.line(&format!(
                                "int64_t {tmp} = mako_grpc_http2_stream_data_count({s}, {st});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "grpc_http2_client_stream_flow" => {
                            let (_, st) = self.emit_expr(&args[0]);
                            let (_, n1) = self.emit_expr(&args[1]);
                            let (_, i1) = self.emit_expr(&args[2]);
                            let (_, n2) = self.emit_expr(&args[3]);
                            let (_, i2) = self.emit_expr(&args[4]);
                            let (_, reply) = self.emit_expr(&args[5]);
                            let (_, rid) = self.emit_expr(&args[6]);
                            let (_, status) = self.emit_expr(&args[7]);
                            let tmp = self.fresh("ghcsf");
                            self.line(&format!(
                                "MakoString {tmp} = mako_grpc_http2_client_stream_flow({st}, {n1}, {i1}, {n2}, {i2}, {reply}, {rid}, {status});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "grpc_stub_ping" => {
                            return ("int64_t".into(), "mako_grpc_stub_ping()".into());
                        }
                        "queue_stub_ping" => {
                            return ("int64_t".into(), "mako_queue_stub_ping()".into());
                        }
                        "sql_query" => {
                            let (_, q) = self.emit_expr(&args[0]);
                            return ("MakoString".into(), format!("mako_sql_query({q})"));
                        }
                        "gc_arena_new" => {
                            let tmp = self.fresh("gc");
                            self.line(&format!("MakoArena {tmp} = mako_gc_arena_new();"));
                            return ("MakoArena".into(), tmp);
                        }
                        "http_bind" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hb");
                            self.line(&format!("int64_t {tmp} = mako_http_bind({p});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http_bind_addr" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("hba");
                            self.line(&format!("int64_t {tmp} = mako_http_bind_addr({h}, {p});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http_accept" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ha");
                            self.line(&format!("int64_t {tmp} = mako_http_accept({f});"));
                            return ("int64_t".into(), tmp);
                        }
                        "http_method" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hm");
                            self.line(&format!("MakoString {tmp} = mako_http_method({c});"));
                            return ("MakoString".into(), tmp);
                        }
                        "http_path" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hp");
                            self.line(&format!("MakoString {tmp} = mako_http_path({c});"));
                            return ("MakoString".into(), tmp);
                        }
                        "http_body" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("hbd");
                            self.line(&format!("MakoString {tmp} = mako_http_body({c});"));
                            return ("MakoString".into(), tmp);
                        }
                        "http_respond" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, st) = self.emit_expr(&args[1]);
                            let (_, b) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("hr");
                            self.line(&format!(
                                "int64_t {tmp} = mako_http_respond({c}, {st}, {b});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_serve" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, cert) = self.emit_expr(&args[1]);
                            let (_, key) = self.emit_expr(&args[2]);
                            let (_, body) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("ts");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_serve({p}, {cert}, {key}, {body});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_serve_once" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, cert) = self.emit_expr(&args[1]);
                            let (_, key) = self.emit_expr(&args[2]);
                            let (_, body) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("tso");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_serve_once({p}, {cert}, {key}, {body});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_serve_n" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, cert) = self.emit_expr(&args[1]);
                            let (_, key) = self.emit_expr(&args[2]);
                            let (_, body) = self.emit_expr(&args[3]);
                            let (_, n) = self.emit_expr(&args[4]);
                            let tmp = self.fresh("tsn");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_serve_n({p}, {cert}, {key}, {body}, {n});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_serve_once_h2" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, cert) = self.emit_expr(&args[1]);
                            let (_, key) = self.emit_expr(&args[2]);
                            let (_, body) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("tsoh");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_serve_once_h2({p}, {cert}, {key}, {body});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_serve_h2_n" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, cert) = self.emit_expr(&args[1]);
                            let (_, key) = self.emit_expr(&args[2]);
                            let (_, body) = self.emit_expr(&args[3]);
                            let (_, n) = self.emit_expr(&args[4]);
                            let tmp = self.fresh("tshn");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_serve_h2_n({p}, {cert}, {key}, {body}, {n});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_serve_h2_routes" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, cert) = self.emit_expr(&args[1]);
                            let (_, key) = self.emit_expr(&args[2]);
                            let (_, root) = self.emit_expr(&args[3]);
                            let (_, health) = self.emit_expr(&args[4]);
                            let (_, n) = self.emit_expr(&args[5]);
                            let tmp = self.fresh("tshr");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_serve_h2_routes({p}, {cert}, {key}, {root}, {health}, {n});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_serve_h2_wu" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, cert) = self.emit_expr(&args[1]);
                            let (_, key) = self.emit_expr(&args[2]);
                            let (_, body) = self.emit_expr(&args[3]);
                            let (_, inc) = self.emit_expr(&args[4]);
                            let tmp = self.fresh("tshw");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_serve_h2_wu({p}, {cert}, {key}, {body}, {inc});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_serve_grpc_once" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, cert) = self.emit_expr(&args[1]);
                            let (_, key) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("tsgo");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_serve_grpc_once({p}, {cert}, {key});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "tls_serve_grpc_stream" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, cert) = self.emit_expr(&args[1]);
                            let (_, key) = self.emit_expr(&args[2]);
                            let tmp = self.fresh("tsgs");
                            self.line(&format!(
                                "int64_t {tmp} = mako_tls_serve_grpc_stream({p}, {cert}, {key});"
                            ));
                            return ("int64_t".into(), tmp);
                        }
                        "io_wait" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, ms) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_io_wait({f}, {ms})"));
                        }
                        "io_poll2" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, ms) = self.emit_expr(&args[2]);
                            return ("int64_t".into(), format!("mako_io_poll2({a}, {b}, {ms})"));
                        }
                        "io_poll3" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, c) = self.emit_expr(&args[2]);
                            let (_, ms) = self.emit_expr(&args[3]);
                            return (
                                "int64_t".into(),
                                format!("mako_io_poll3({a}, {b}, {c}, {ms})"),
                            );
                        }
                        "io_poll4" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, c) = self.emit_expr(&args[2]);
                            let (_, d) = self.emit_expr(&args[3]);
                            let (_, ms) = self.emit_expr(&args[4]);
                            return (
                                "int64_t".into(),
                                format!("mako_io_poll4({a}, {b}, {c}, {d}, {ms})"),
                            );
                        }
                        "io_kq_poll2" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, ms) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_io_kq_poll2({a}, {b}, {ms})"),
                            );
                        }
                        "io_epoll_poll2" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, ms) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_io_epoll_poll2({a}, {b}, {ms})"),
                            );
                        }
                        "io_native_poll2" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, ms) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_io_native_poll2({a}, {b}, {ms})"),
                            );
                        }
                        "io_read_ready" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, ms) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_io_read_ready({f}, {ms})"));
                        }
                        "io_write_ready" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, ms) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_io_write_ready({f}, {ms})"));
                        }
                        "io_set_nonblocking" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, enabled) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_io_set_nonblocking({f}, {enabled})"),
                            );
                        }
                        "io_try_write" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, data) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_io_try_write({f}, {data})"));
                        }
                        "io_backoff_ms" => {
                            let (_, attempt) = self.emit_expr(&args[0]);
                            let (_, min_ms) = self.emit_expr(&args[1]);
                            let (_, max_ms) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_io_backoff_ms({attempt}, {min_ms}, {max_ms})"),
                            );
                        }
                        "io_should_pause" => {
                            let (_, pending) = self.emit_expr(&args[0]);
                            let (_, high) = self.emit_expr(&args[1]);
                            let (_, writable) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_io_should_pause({pending}, {high}, {writable})"),
                            );
                        }
                        "tcp_accept_nb" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_tcp_accept_nb({f})"));
                        }
                        "tcp_connect" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_tcp_connect({h}, {p})"));
                        }
                        "tcp_connect_nb" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            return ("int64_t".into(), format!("mako_tcp_connect_nb({h}, {p})"));
                        }
                        "tcp_connect_check" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_tcp_connect_check({f})"));
                        }
                        "tcp_connect_wait" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, ms) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_tcp_connect_wait({f}, {ms})"),
                            );
                        }
                        "tcp_pool_open" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, m) = self.emit_expr(&args[2]);
                            let (_, ms) = self.emit_expr(&args[3]);
                            return (
                                "int64_t".into(),
                                format!("mako_tcp_pool_open({h}, {p}, {m}, {ms})"),
                            );
                        }
                        "tcp_pool_acquire" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_tcp_pool_acquire({p})"));
                        }
                        "tcp_pool_release" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            let (_, f) = self.emit_expr(&args[1]);
                            let (_, r) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_tcp_pool_release({p}, {f}, {r})"),
                            );
                        }
                        "tcp_pool_close" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_tcp_pool_close({p})"));
                        }
                        "tcp_pool_idle" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_tcp_pool_idle({p})"));
                        }
                        "tcp_pool_open_count" => {
                            let (_, p) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_tcp_pool_open_count({p})"));
                        }
                        "tcp_fd_copy" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            let (_, m) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_tcp_fd_copy({s}, {d}, {m})"),
                            );
                        }
                        "tcp_splice" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let (_, d) = self.emit_expr(&args[1]);
                            let (_, m) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_tcp_splice({s}, {d}, {m})"),
                            );
                        }
                        "tcp_proxy_pump" => {
                            let (_, a) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, ms) = self.emit_expr(&args[2]);
                            let (_, m) = self.emit_expr(&args[3]);
                            return (
                                "int64_t".into(),
                                format!("mako_tcp_proxy_pump({a}, {b}, {ms}, {m})"),
                            );
                        }
                        "tcp_set_recv_buf" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_tcp_set_recv_buf({f}, {s})"),
                            );
                        }
                        "tcp_set_send_buf" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            return (
                                "int64_t".into(),
                                format!("mako_tcp_set_send_buf({f}, {s})"),
                            );
                        }
                        "tcp_reuseport" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_tcp_reuseport({f})"));
                        }
                        "tcp_listen_reuseport" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, bl) = self.emit_expr(&args[2]);
                            return (
                                "int64_t".into(),
                                format!("mako_tcp_listen_reuseport({h}, {p}, {bl})"),
                            );
                        }
                        "tcp_accept4" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_tcp_accept4({f})"));
                        }
                        "http_forward" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, m) = self.emit_expr(&args[2]);
                            let (_, path) = self.emit_expr(&args[3]);
                            let (_, body) = self.emit_expr(&args[4]);
                            let tmp = self.fresh("fwd");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_forward({h}, {p}, {m}, {path}, {body});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_forward_full" => {
                            let (_, h) = self.emit_expr(&args[0]);
                            let (_, p) = self.emit_expr(&args[1]);
                            let (_, m) = self.emit_expr(&args[2]);
                            let (_, path) = self.emit_expr(&args[3]);
                            let (_, headers) = self.emit_expr(&args[4]);
                            let (_, body) = self.emit_expr(&args[5]);
                            let (_, ms) = self.emit_expr(&args[6]);
                            let tmp = self.fresh("fwd_full");
                            self.line(&format!(
                                "MakoHttpForwardResult {tmp} = mako_http_forward_full({h}, {p}, {m}, {path}, {headers}, {body}, {ms});"
                            ));
                            return ("MakoHttpForwardResult".into(), tmp);
                        }
                        "http_forward_fd" => {
                            let (_, f) = self.emit_expr(&args[0]);
                            let (_, m) = self.emit_expr(&args[1]);
                            let (_, path) = self.emit_expr(&args[2]);
                            let (_, host) = self.emit_expr(&args[3]);
                            let (_, headers) = self.emit_expr(&args[4]);
                            let (_, body) = self.emit_expr(&args[5]);
                            let (_, ms) = self.emit_expr(&args[6]);
                            let tmp = self.fresh("fwd_fd");
                            self.line(&format!(
                                "MakoHttpForwardResult {tmp} = mako_http_forward_fd({f}, {m}, {path}, {host}, {headers}, {body}, {ms});"
                            ));
                            return ("MakoHttpForwardResult".into(), tmp);
                        }
                        "http_forward_ok" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_http_forward_ok({r})"));
                        }
                        "http_forward_status" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_http_forward_status({r})"));
                        }
                        "http_forward_body_len" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_http_forward_body_len({r})"),
                            );
                        }
                        "http_forward_total_bytes" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_http_forward_total_bytes({r})"),
                            );
                        }
                        "http_forward_body" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("fwd_body");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_forward_body({r});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_forward_headers" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("fwd_hdr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_forward_headers({r});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_proxy_raw" => {
                            let (_, c) = self.emit_expr(&args[0]);
                            let (_, b) = self.emit_expr(&args[1]);
                            let (_, req) = self.emit_expr(&args[2]);
                            let (_, ms) = self.emit_expr(&args[3]);
                            let tmp = self.fresh("proxy_raw");
                            self.line(&format!(
                                "MakoProxyIoResult {tmp} = mako_http_proxy_raw({c}, {b}, {req}, {ms});"
                            ));
                            return ("MakoProxyIoResult".into(), tmp);
                        }
                        "proxy_io_ok" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_proxy_io_ok({r})"));
                        }
                        "proxy_io_bytes_written" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_proxy_io_bytes_written({r})"),
                            );
                        }
                        "proxy_io_bytes_read" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_proxy_io_bytes_read({r})"),
                            );
                        }
                        "http_parse" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("http_parsed");
                            self.line(&format!(
                                "MakoHttpParsed {tmp} = mako_http_parse({s});"
                            ));
                            return ("MakoHttpParsed".into(), tmp);
                        }
                        "http_parsed_ok" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            return ("int64_t".into(), format!("mako_http_parsed_ok({r})"));
                        }
                        "http_parsed_content_length" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_http_parsed_content_length({r})"),
                            );
                        }
                        "http_parsed_chunked" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            return (
                                "int64_t".into(),
                                format!("mako_http_parsed_chunked({r})"),
                            );
                        }
                        "http_parsed_method" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pm");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_parsed_method({r});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_parsed_path" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pp");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_parsed_path({r});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_parsed_host" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("ph");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_parsed_host({r});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_parsed_headers" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("phdr");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_parsed_headers({r});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_parsed_body" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("pb");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_parsed_body({r});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_parsed_header" => {
                            let (_, r) = self.emit_expr(&args[0]);
                            let (_, n) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("phdr1");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_parsed_header({r}, {n});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "http_decode_chunked" => {
                            let (_, s) = self.emit_expr(&args[0]);
                            let tmp = self.fresh("chunked");
                            self.line(&format!(
                                "MakoString {tmp} = mako_http_decode_chunked({s});"
                            ));
                            return ("MakoString".into(), tmp);
                        }
                        "chan_select_value" => {
                            return ("int64_t".into(), "mako_chan_select_value()".into());
                        }
                        "len" => {
                            let (ty, v) = self.emit_expr(&args[0]);
                            if ty == "MakoString" {
                                return ("int64_t".into(), format!("mako_str_len({v})"));
                            }
                            if ty == "MakoByteArray" {
                                return ("int64_t".into(), format!("mako_byte_array_len({v})"));
                            }
                            if ty == "MakoStrArray" {
                                return ("int64_t".into(), format!("mako_str_array_len({v})"));
                            }
                            if ty == "MakoFloatArray" {
                                return ("int64_t".into(), format!("mako_float_array_len({v})"));
                            }
                            if let Some(sn) = ty.strip_prefix("MakoArr_") {
                                return ("int64_t".into(), format!("mako_arr_{sn}_len({v})"));
                            }
                            if ty == "MakoMapSI*" {
                                return ("int64_t".into(), format!("mako_map_si_len({v})"));
                            }
                            if ty == "MakoMapII*" {
                                return ("int64_t".into(), format!("mako_map_ii_len({v})"));
                            }
                            if ty == "MakoMapSS*" {
                                return ("int64_t".into(), format!("mako_map_ss_len({v})"));
                            }
                            if ty == "MakoStrBuilder*" {
                                return ("int64_t".into(), format!("mako_str_builder_len({v})"));
                            }
                            return ("int64_t".into(), format!("mako_array_len({v})"));
                        }
                        "delete" => {
                            let (ty, m) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            if ty == "MakoMapSI*" {
                                self.line(&format!("mako_map_si_delete({m}, {k});"));
                            } else if ty == "MakoMapII*" {
                                self.line(&format!("mako_map_ii_delete({m}, {k});"));
                            } else if ty == "MakoMapSS*" {
                                self.line(&format!("mako_map_ss_delete({m}, {k});"));
                            }
                            return ("void".into(), "/*void*/".into());
                        }
                        "has" => {
                            let (ty, m) = self.emit_expr(&args[0]);
                            let (_, k) = self.emit_expr(&args[1]);
                            if ty == "MakoMapSI*" {
                                return ("bool".into(), format!("mako_map_si_has({m}, {k})"));
                            }
                            if ty == "MakoMapII*" {
                                return ("bool".into(), format!("mako_map_ii_has({m}, {k})"));
                            }
                            if ty == "MakoMapSS*" {
                                return ("bool".into(), format!("mako_map_ss_has({m}, {k})"));
                            }
                            return ("bool".into(), "false".into());
                        }
                        "cap" => {
                            let (ty, v) = self.emit_expr(&args[0]);
                            if ty == "MakoByteArray" {
                                return ("int64_t".into(), format!("mako_byte_array_cap({v})"));
                            }
                            if ty == "MakoStrArray" {
                                return ("int64_t".into(), format!("mako_str_array_cap({v})"));
                            }
                            if ty == "MakoFloatArray" {
                                return ("int64_t".into(), format!("mako_float_array_cap({v})"));
                            }
                            if let Some(sn) = ty.strip_prefix("MakoArr_") {
                                return ("int64_t".into(), format!("mako_arr_{sn}_cap({v})"));
                            }
                            return ("int64_t".into(), format!("mako_array_cap({v})"));
                        }
                        "append" => {
                            let (sty, s) = self.emit_expr(&args[0]);
                            let (_, v) = self.emit_expr(&args[1]);
                            let tmp = self.fresh("ap");
                            if sty == "MakoByteArray" {
                                self.line(&format!(
                                    "MakoByteArray {tmp} = mako_byte_append({s}, {v});"
                                ));
                                return ("MakoByteArray".into(), tmp);
                            }
                            if sty == "MakoStrArray" {
                                self.line(&format!(
                                    "MakoStrArray {tmp} = mako_str_array_append({s}, {v});"
                                ));
                                return ("MakoStrArray".into(), tmp);
                            }
                            if sty == "MakoFloatArray" {
                                self.line(&format!(
                                    "MakoFloatArray {tmp} = mako_float_array_append({s}, {v});"
                                ));
                                return ("MakoFloatArray".into(), tmp);
                            }
                            if let Some(sn) = sty.strip_prefix("MakoArr_") {
                                let sn = sn.to_string();
                                if let Some(arena) = self.current_arena.clone() {
                                    self.line(&format!(
                                        "MakoArr_{sn} {tmp} = mako_arr_{sn}_arena_append(&{arena}, {s}, {v});"
                                    ));
                                } else {
                                    self.line(&format!(
                                        "MakoArr_{sn} {tmp} = mako_arr_{sn}_append({s}, {v});"
                                    ));
                                }
                                return (format!("MakoArr_{sn}"), tmp);
                            }
                            self.line(&format!(
                                "MakoIntArray {tmp} = mako_slice_append({s}, {v});"
                            ));
                            return ("MakoIntArray".into(), tmp);
                        }
                        "copy" => {
                            let (dty, d) = self.emit_expr(&args[0]);
                            let (_, s) = self.emit_expr(&args[1]);
                            if dty == "MakoByteArray" {
                                return ("int64_t".into(), format!("mako_byte_copy({d}, {s})"));
                            }
                            if dty == "MakoStrArray" {
                                return (
                                    "int64_t".into(),
                                    format!("mako_str_array_copy({d}, {s})"),
                                );
                            }
                            if dty == "MakoFloatArray" {
                                return (
                                    "int64_t".into(),
                                    format!("mako_float_array_copy({d}, {s})"),
                                );
                            }
                            return ("int64_t".into(), format!("mako_slice_copy({d}, {s})"));
                        }
                        _ => {
                            // User enum variant constructor?
                            if let Some(enum_name) = self.variant_to_enum.get(name).cloned() {
                                return self.emit_enum_construct(&enum_name, name, args);
                            }
                            // Emit args first (needed for generic mono tag inference).
                            let mut arg_tys = Vec::new();
                            let mut arg_raw = Vec::new();
                            for a in args {
                                let (aty, v) = self.emit_expr(a);
                                arg_tys.push(aty);
                                arg_raw.push(v);
                            }
                            // Generic call: `id(x)` → `id__int(x)` monomorphization.
                            // Tag only by type *parameters* (not each argument), matching typecheck.
                            let resolved = if let Some(tmpl) = self.generic_templates.get(name) {
                                let mut tags = Vec::new();
                                for tp in &tmpl.type_params {
                                    // Find first param whose type is this type param
                                    let mut found = None;
                                    for (i, p) in tmpl.params.iter().enumerate() {
                                        if matches!(&p.ty, TypeExpr::Named(n) if n == tp) {
                                            if let Some(aty) = arg_tys.get(i) {
                                                found = Some(c_type_mono_tag(aty));
                                            }
                                            break;
                                        }
                                    }
                                    tags.push(found.unwrap_or_else(|| "int".into()));
                                }
                                format!("{name}__{}", tags.join("__"))
                            } else {
                                name.clone()
                            };
                            let expected = self
                                .fn_params
                                .get(&resolved)
                                .cloned()
                                .or_else(|| self.fn_params.get(name).cloned())
                                .unwrap_or_default();
                            let mut arg_vals = Vec::new();
                            for (i, (aty, v)) in arg_tys.iter().zip(arg_raw.into_iter()).enumerate()
                            {
                                let v = if let Some(exp) = expected.get(i) {
                                    if exp.starts_with("MakoIface_") && aty != exp {
                                        let iname = &exp["MakoIface_".len()..];
                                        let box_fn = format!("mako_iface_{iname}_from_{aty}");
                                        let tmp = self.fresh("ibox");
                                        self.line(&format!("{exp} {tmp} = {box_fn}({v});"));
                                        tmp
                                    } else {
                                        v
                                    }
                                } else {
                                    v
                                };
                                arg_vals.push(v);
                            }
                            let call_name = if self.extern_fns.contains(&resolved)
                                || self.extern_fns.contains(name)
                            {
                                resolved.clone()
                            } else {
                                mangle(&resolved)
                            };
                            let call = format!("{call_name}({})", arg_vals.join(", "));
                            let tmp = self.fresh("r");
                            if name == "main" {
                                self.line(&format!("{call};"));
                                return ("void".into(), "/*void*/".into());
                            }
                            let ret = self
                                .fn_rets
                                .get(&resolved)
                                .cloned()
                                .or_else(|| self.fn_rets.get(name).cloned())
                                .unwrap_or_else(|| "int64_t".into());
                            if ret == "void" {
                                self.line(&format!("{call};"));
                                return ("void".into(), "/*void*/".into());
                            }
                            self.line(&format!("{ret} {tmp} = {call};"));
                            return (ret, tmp);
                        }
                    }
                }
                ("int64_t".into(), "0".into())
            }
            Expr::StructLit { name, fields } => {
                let cty = self
                    .structs
                    .get(name)
                    .map(|s| s.c_name.clone())
                    .unwrap_or_else(|| name.clone());
                let tmp = self.fresh("st");
                self.line(&format!("{cty} {tmp};"));
                self.line(&format!("memset(&{tmp}, 0, sizeof({tmp}));"));
                for (fname, fexpr) in fields {
                    let (_, v) = self.emit_expr(fexpr);
                    self.line(&format!("{tmp}.{fname} = {v};"));
                }
                (cty, tmp)
            }
            Expr::StructLitPos { name, values } => {
                // Resolve positional values to declared field names (in order).
                let info = self.structs.get(name).cloned();
                let cty = info
                    .as_ref()
                    .map(|s| s.c_name.clone())
                    .unwrap_or_else(|| name.clone());
                let field_names: Vec<String> = info
                    .map(|s| s.fields.iter().map(|(n, _)| n.clone()).collect())
                    .unwrap_or_default();
                let tmp = self.fresh("st");
                self.line(&format!("{cty} {tmp};"));
                self.line(&format!("memset(&{tmp}, 0, sizeof({tmp}));"));
                for (i, vexpr) in values.iter().enumerate() {
                    let (_, v) = self.emit_expr(vexpr);
                    if let Some(fname) = field_names.get(i) {
                        self.line(&format!("{tmp}.{fname} = {v};"));
                    }
                }
                (cty, tmp)
            }
            Expr::Tuple(elems) => {
                let mut tys = Vec::new();
                let mut vals = Vec::new();
                for e in elems {
                    let (t, v) = self.emit_expr(e);
                    tys.push(t);
                    vals.push(v);
                }
                let tag = tys
                    .iter()
                    .map(|t| c_type_mono_tag(t))
                    .collect::<Vec<_>>()
                    .join("_");
                let cname = format!("MakoTup_{tag}");
                if self.tuple_typedefs.insert(cname.clone()) {
                    let mut fields = String::new();
                    for (i, t) in tys.iter().enumerate() {
                        fields.push_str(&format!("    {t} _{i};\n"));
                    }
                    // Insert at the helpers marker so an inline tuple literal (e.g.
                    // parallel assignment `a, b = b, a`) has its type declared even
                    // when first seen during body emission.
                    self.insert_helper(&format!(
                        "typedef struct {{\n{fields}}} {cname};\n"
                    ));
                }
                let tmp = self.fresh("tup");
                self.line(&format!("{cname} {tmp};"));
                for (i, v) in vals.iter().enumerate() {
                    self.line(&format!("{tmp}._{i} = {v};"));
                }
                (cname, tmp)
            }
            Expr::ChanOpen { elem, cap } => {
                let (_, c) = self.emit_expr(cap);
                let tmp = self.fresh("ch");
                match elem {
                    TypeExpr::Named(n) if n == "string" => {
                        self.line(&format!(
                            "MakoChanStr *{tmp} = mako_chan_str_new({c});"
                        ));
                        ("MakoChanStr*".into(), tmp)
                    }
                    TypeExpr::Named(n) if n == "float" || n == "float64" => {
                        self.line(&format!("MakoChan *{tmp} = mako_chan_new({c});"));
                        self.chan_float.insert(tmp.clone());
                        ("MakoChan*".into(), tmp)
                    }
                    TypeExpr::Named(n)
                        if n != "int"
                            && n != "int64"
                            && n != "int32"
                            && n != "int8"
                            && n != "byte"
                            && n != "bool"
                            && n != "float"
                            && n != "float64"
                            && n != "string"
                            && self.structs.contains_key(n) =>
                    {
                        self.line(&format!(
                            "MakoChanPtr *{tmp} = mako_chan_ptr_new({c});"
                        ));
                        // Remember element type for this temporary / binding
                        self.chan_ptr_elems.insert(tmp.clone(), n.clone());
                        ("MakoChanPtr*".into(), tmp)
                    }
                    _ => {
                        self.line(&format!("MakoChan *{tmp} = mako_chan_new({c});"));
                        ("MakoChan*".into(), tmp)
                    }
                }
            }
            Expr::Array(elems) => {
                if !elems.is_empty() && elems.iter().all(|e| matches!(e, Expr::String(_))) {
                    return self.emit_str_array_lit(elems);
                }
                if !elems.is_empty() && elems.iter().all(|e| matches!(e, Expr::Float(_))) {
                    return self.emit_float_array_lit(elems);
                }
                if !elems.is_empty() && elems.iter().all(|e| struct_lit_name(e).is_some()) {
                    if let Some(name) = struct_lit_name(&elems[0]) {
                        let sn = name.to_string();
                        return self.emit_struct_array_lit(&sn, elems);
                    }
                }
                let tmp = self.fresh("arr");
                let vals: Vec<String> = elems
                    .iter()
                    .map(|e| {
                        let (_, v) = self.emit_expr(e);
                        v
                    })
                    .collect();
                let lit = self.fresh("lit");
                self.line(&format!("int64_t {lit}[] = {{ {} }};", vals.join(", ")));
                self.line(&format!(
                    "MakoIntArray {tmp} = mako_int_array_of({lit}, {});",
                    elems.len()
                ));
                ("MakoIntArray".into(), tmp)
            }
            Expr::Convert { ty, args } => {
                let is_byte = matches!(
                    ty,
                    TypeExpr::Array(inner)
                        if matches!(inner.as_ref(), TypeExpr::Named(n) if n == "byte")
                );
                if is_byte && args.len() == 1 {
                    let (aty, v) = self.emit_expr(&args[0]);
                    if aty == "MakoByteArray" {
                        return (aty, v);
                    }
                    let tmp = self.fresh("by");
                    self.line(&format!(
                        "MakoByteArray {tmp} = mako_bytes_from_string({v});"
                    ));
                    return ("MakoByteArray".into(), tmp);
                }
                ("int64_t".into(), "0".into())
            }
            Expr::Make { ty, len, cap } => {
                // make(chan[T], n) — typed channel open
                if let TypeExpr::Generic(n, args) = ty {
                    if n == "chan" && args.len() == 1 {
                        let cap_e = len.as_ref().map(|e| e.as_ref()).or(cap.as_ref().map(|e| e.as_ref()));
                        let c = if let Some(ce) = cap_e {
                            let (_, v) = self.emit_expr(ce);
                            v
                        } else {
                            "1".into()
                        };
                        let tmp = self.fresh("ch");
                        match &args[0] {
                            TypeExpr::Named(en) if en == "string" => {
                                self.line(&format!(
                                    "MakoChanStr *{tmp} = mako_chan_str_new({c});"
                                ));
                                return ("MakoChanStr*".into(), tmp);
                            }
                            _ => {
                                self.line(&format!("MakoChan *{tmp} = mako_chan_new({c});"));
                                return ("MakoChan*".into(), tmp);
                            }
                        }
                    }
                }
                if let TypeExpr::Map(k, v) = ty {
                    let hint = if let Some(l) = len {
                        let (_, h) = self.emit_expr(l);
                        h
                    } else {
                        "0".into()
                    };
                    let tmp = self.fresh("mk");
                    let cty = match (k.as_ref(), v.as_ref()) {
                        (TypeExpr::Named(kk), TypeExpr::Named(vv))
                            if kk == "string" && vv == "int" =>
                        {
                            self.line(&format!("MakoMapSI *{tmp} = mako_map_si_make({hint});"));
                            "MakoMapSI*"
                        }
                        (TypeExpr::Named(kk), TypeExpr::Named(vv))
                            if kk == "int" && vv == "int" =>
                        {
                            self.line(&format!("MakoMapII *{tmp} = mako_map_ii_make({hint});"));
                            "MakoMapII*"
                        }
                        (TypeExpr::Named(kk), TypeExpr::Named(vv))
                            if kk == "string" && vv == "string" =>
                        {
                            self.line(&format!("MakoMapSS *{tmp} = mako_map_ss_make({hint});"));
                            "MakoMapSS*"
                        }
                        _ => {
                            self.line(&format!("MakoMapSI *{tmp} = mako_map_si_make({hint});"));
                            "MakoMapSI*"
                        }
                    };
                    return (cty.into(), tmp);
                }
                let is_byte = matches!(
                    ty,
                    TypeExpr::Array(inner)
                        if matches!(inner.as_ref(), TypeExpr::Named(n) if n == "byte")
                );
                let is_string = matches!(
                    ty,
                    TypeExpr::Array(inner)
                        if matches!(inner.as_ref(), TypeExpr::Named(n) if n == "string")
                );
                let is_float = matches!(
                    ty,
                    TypeExpr::Array(inner)
                        if matches!(inner.as_ref(), TypeExpr::Named(n) if n == "float" || n == "float64")
                );
                let struct_elem = match ty {
                    TypeExpr::Array(inner) => match inner.as_ref() {
                        TypeExpr::Named(n) if self.structs.contains_key(n) => Some(n.clone()),
                        _ => None,
                    },
                    _ => None,
                };
                let l = if let Some(len_e) = len {
                    let (_, v) = self.emit_expr(len_e);
                    v
                } else {
                    "0".into()
                };
                let c = if let Some(cap_e) = cap {
                    let (_, v) = self.emit_expr(cap_e);
                    v
                } else {
                    l.clone()
                };
                let tmp = self.fresh("mk");
                if let Some(arena) = self.current_arena.clone() {
                    if is_byte {
                        self.line(&format!(
                            "MakoByteArray {tmp} = mako_arena_byte_array_make(&{arena}, {l}, {c});"
                        ));
                        return ("MakoByteArray".into(), tmp);
                    }
                    if is_string {
                        self.line(&format!(
                            "MakoStrArray {tmp} = mako_arena_str_array_make(&{arena}, {l}, {c});"
                        ));
                        return ("MakoStrArray".into(), tmp);
                    }
                    if is_float {
                        self.line(&format!(
                            "MakoFloatArray {tmp} = mako_arena_float_array_make(&{arena}, {l}, {c});"
                        ));
                        return ("MakoFloatArray".into(), tmp);
                    }
                    if let Some(sn) = struct_elem.clone() {
                        self.line(&format!(
                            "MakoArr_{sn} {tmp} = mako_arr_{sn}_arena_make(&{arena}, {l}, {c});"
                        ));
                        return (format!("MakoArr_{sn}"), tmp);
                    }
                    self.line(&format!(
                        "MakoIntArray {tmp} = mako_arena_int_array_make(&{arena}, {l}, {c});"
                    ));
                    return ("MakoIntArray".into(), tmp);
                }
                if is_byte {
                    self.line(&format!(
                        "MakoByteArray {tmp} = mako_byte_array_make({l}, {c});"
                    ));
                    ("MakoByteArray".into(), tmp)
                } else if is_string {
                    self.line(&format!(
                        "MakoStrArray {tmp} = mako_str_array_make({l}, {c});"
                    ));
                    ("MakoStrArray".into(), tmp)
                } else if is_float {
                    self.line(&format!(
                        "MakoFloatArray {tmp} = mako_float_array_make({l}, {c});"
                    ));
                    ("MakoFloatArray".into(), tmp)
                } else if let Some(sn) = struct_elem {
                    self.line(&format!(
                        "MakoArr_{sn} {tmp} = mako_arr_{sn}_make({l}, {c});"
                    ));
                    (format!("MakoArr_{sn}"), tmp)
                } else {
                    self.line(&format!(
                        "MakoIntArray {tmp} = mako_int_array_make({l}, {c});"
                    ));
                    ("MakoIntArray".into(), tmp)
                }
            }
            Expr::Index { base, index } => {
                let (bty, b) = self.emit_expr(base);
                let (_, i) = self.emit_expr(index);
                if bty == "MakoMapSI*" {
                    return ("int64_t".into(), format!("mako_map_si_get({b}, {i})"));
                }
                if bty == "MakoMapII*" {
                    return ("int64_t".into(), format!("mako_map_ii_get({b}, {i})"));
                }
                if bty == "MakoMapSS*" {
                    let tmp = self.fresh("mg");
                    self.line(&format!("MakoString {tmp} = mako_map_ss_get({b}, {i});"));
                    return ("MakoString".into(), tmp);
                }
                let tmp = self.fresh("idx");
                self.line(&format!("int64_t {tmp} = {i};"));
                if bty == "MakoByteArray" {
                    return ("int64_t".into(), format!("mako_byte_get({b}, {tmp})"));
                }
                if bty == "MakoStrArray" {
                    let out = self.fresh("sg");
                    self.line(&format!(
                        "MakoString {out} = mako_str_array_get({b}, {tmp});"
                    ));
                    return ("MakoString".into(), out);
                }
                if bty == "MakoFloatArray" {
                    return ("double".into(), format!("mako_float_array_get({b}, {tmp})"));
                }
                if let Some(sn) = bty.strip_prefix("MakoArr_") {
                    let out = self.fresh("sg");
                    self.line(&format!("{sn} {out} = mako_arr_{sn}_get({b}, {tmp});"));
                    return (sn.to_string(), out);
                }
                if bty == "MakoString" {
                    return ("int64_t".into(), format!("mako_str_get({b}, {tmp})"));
                }
                self.emit_bounds_check(
                    &format!("{tmp} < 0 || (size_t){tmp} >= {b}.len"),
                    "index out of bounds (slices are 0..len-1)",
                );
                ("int64_t".into(), format!("{b}.data[{tmp}]"))
            }
            Expr::Slice {
                base,
                low,
                high,
                max,
            } => {
                let (bty, b) = self.emit_expr(base);
                let low_c = if let Some(l) = low {
                    let (_, v) = self.emit_expr(l);
                    v
                } else {
                    "0".into()
                };
                let high_c = if let Some(h) = high {
                    let (_, v) = self.emit_expr(h);
                    v
                } else {
                    format!("(int64_t){b}.len")
                };
                let (has_max, max_c) = if let Some(m) = max {
                    let (_, v) = self.emit_expr(m);
                    (1, v)
                } else {
                    (0, "0".into())
                };
                let tmp = self.fresh("sl");
                if bty == "MakoString" {
                    self.line(&format!(
                        "MakoString {tmp} = mako_str_slice({b}, {low_c}, {high_c});"
                    ));
                    return ("MakoString".into(), tmp);
                }
                if bty == "MakoByteArray" {
                    self.line(&format!(
                        "MakoByteArray {tmp} = mako_byte_slice_expr({b}, {low_c}, {high_c}, {max_c}, {has_max});"
                    ));
                    return ("MakoByteArray".into(), tmp);
                }
                if bty == "MakoStrArray" {
                    self.line(&format!(
                        "MakoStrArray {tmp} = mako_str_array_slice_expr({b}, {low_c}, {high_c}, {max_c}, {has_max});"
                    ));
                    return ("MakoStrArray".into(), tmp);
                }
                if bty == "MakoFloatArray" {
                    self.line(&format!(
                        "MakoFloatArray {tmp} = mako_float_array_slice_expr({b}, {low_c}, {high_c}, {max_c}, {has_max});"
                    ));
                    return ("MakoFloatArray".into(), tmp);
                }
                self.line(&format!(
                    "MakoIntArray {tmp} = mako_slice_expr({b}, {low_c}, {high_c}, {max_c}, {has_max});"
                ));
                ("MakoIntArray".into(), tmp)
            }
            Expr::Try(inner) => {
                let (_, v) = self.emit_expr(inner);
                let tmp = self.fresh("try");
                self.line(&format!("MakoResultInt {tmp} = {v};"));
                self.line(&format!("if (!{tmp}.ok) return {tmp};"));
                ("int64_t".into(), format!("{tmp}.value"))
            }
            Expr::Kick { crew, expr } => {
                let task = self.fresh("job");
                match expr.as_ref() {
                    Expr::Call { callee: c, args } => {
                        if let Expr::Ident(fname) = c.as_ref() {
                            let param_tys = self
                                .fn_params
                                .get(fname)
                                .cloned()
                                .unwrap_or_else(|| vec!["int64_t".into(); args.len()]);
                            let arg_name = self.fresh("arg");
                            if args.is_empty() {
                                self.line(&format!("intptr_t *{arg_name} = NULL;"));
                            } else {
                                self.line(&format!(
                                    "intptr_t *{arg_name} = (intptr_t*)malloc(sizeof(intptr_t) * {});",
                                    args.len()
                                ));
                                for (i, a) in args.iter().enumerate() {
                                    let (aty, v) = self.emit_expr(a);
                                    let pty =
                                        param_tys.get(i).map(|s| s.as_str()).unwrap_or("int64_t");
                                    // Multi-word values: heap-box (and clone where needed) so
                                    // intptr_t packing is safe across pthread spawn.
                                    if aty == "MakoShareInt" || pty == "MakoShareInt" {
                                        let boxn = self.fresh("shbox");
                                        self.line(&format!(
                                            "MakoShareInt *{boxn} = (MakoShareInt*)malloc(sizeof(MakoShareInt));"
                                        ));
                                        self.line(&format!(
                                            "*{boxn} = mako_share_clone({v});"
                                        ));
                                        self.line(&format!(
                                            "{arg_name}[{i}] = (intptr_t){boxn};"
                                        ));
                                    } else if aty == "MakoString" || pty == "MakoString" {
                                        let boxn = self.fresh("sbox");
                                        self.line(&format!(
                                            "MakoString *{boxn} = (MakoString*)malloc(sizeof(MakoString));"
                                        ));
                                        self.line(&format!(
                                            "*{boxn} = mako_str_clone({v});"
                                        ));
                                        self.line(&format!(
                                            "{arg_name}[{i}] = (intptr_t){boxn};"
                                        ));
                                    } else if aty == "double" || pty == "double" {
                                        self.line(&format!(
                                            "{arg_name}[{i}] = (intptr_t)mako_f64_to_bits({v});"
                                        ));
                                    } else if self.structs.contains_key(&aty)
                                        || self.structs.values().any(|s| s.c_name == aty)
                                        || self.structs.contains_key(pty)
                                        || self.structs.values().any(|s| s.c_name == *pty)
                                    {
                                        // POD struct: heap-box copy; clone string fields.
                                        let cname = if self.structs.contains_key(&aty) {
                                            aty.clone()
                                        } else if self.structs.contains_key(pty) {
                                            pty.to_string()
                                        } else {
                                            aty.clone()
                                        };
                                        let fields = self
                                            .structs
                                            .get(&cname)
                                            .or_else(|| {
                                                self.structs.values().find(|s| s.c_name == cname)
                                            })
                                            .map(|s| s.fields.clone())
                                            .unwrap_or_default();
                                        let boxn = self.fresh("podbox");
                                        self.line(&format!(
                                            "{cname} *{boxn} = ({cname}*)malloc(sizeof({cname}));"
                                        ));
                                        self.line(&format!("*{boxn} = {v};"));
                                        for (fname, ft) in &fields {
                                            if ft == "MakoString" {
                                                self.line(&format!(
                                                    "{boxn}->{fname} = mako_str_clone({v}.{fname});"
                                                ));
                                            }
                                        }
                                        self.line(&format!(
                                            "{arg_name}[{i}] = (intptr_t){boxn};"
                                        ));
                                    } else if pty.contains('*') || aty.contains('*') {
                                        self.line(&format!("{arg_name}[{i}] = (intptr_t){v};"));
                                    } else {
                                        self.line(&format!("{arg_name}[{i}] = (intptr_t){v};"));
                                    }
                                }
                            }
                            let helper = format!("__kick_{}_{}", mangle(fname), self.tmp);
                            self.tmp += 1;
                            self.emit_spawn_helper_typed(&helper, fname, &param_tys);
                            self.line(&format!(
                                "MakoTask *{task} = mako_spawn(&{crew}, {helper}, {arg_name});"
                            ));
                            let ret = self
                                .fn_rets
                                .get(fname)
                                .cloned()
                                .unwrap_or_else(|| "int64_t".into());
                            self.job_rets.insert(task.clone(), ret);
                            if let Some(ok) = self.fn_result_ok_kind.get(fname).cloned() {
                                self.job_ok_kinds.insert(task.clone(), ok);
                            }
                            return ("MakoTask*".into(), task);
                        }
                    }
                    _ => {
                        self.line("/* kick of non-call not supported in v0.1 */");
                    }
                }
                ("MakoTask*".into(), task)
            }
            Expr::Join(inner) => {
                let (_, t) = self.emit_expr(inner);
                let ret_ty = match inner.as_ref() {
                    Expr::Ident(n) => self.job_rets.get(n).cloned(),
                    _ => self.job_rets.get(&t).cloned(),
                }
                .unwrap_or_else(|| "int64_t".into());
                self.emit_job_join(&t, &ret_ty)
            }
            Expr::Method {
                receiver,
                method,
                args,
            } => {
                // Import alias: `foo.bar(...)` → call `foo__bar(...)`
                if let Expr::Ident(alias) = receiver.as_ref() {
                    let mangled = format!("{alias}__{method}");
                    if self.fn_rets.contains_key(&mangled) {
                        return self.emit_expr(&Expr::Call {
                            callee: Box::new(Expr::Ident(mangled)),
                            args: args.clone(),
                        });
                    }
                }
                let (rty, rv) = self.emit_expr(receiver);
                match method.as_str() {
                    "send" => {
                        let (vty, v) = self.emit_expr(&args[0]);
                        let tmp = self.fresh("ok");
                        if rty == "MakoChanStr*" {
                            self.line(&format!(
                                "bool {tmp} = mako_chan_str_send({rv}, {v}) != 0;"
                            ));
                        } else if rty == "MakoChanPtr*" {
                            let st = self
                                .chan_ptr_elems
                                .get(&rv)
                                .cloned()
                                .or_else(|| {
                                    // receiver might be expression; try local name
                                    if let Expr::Ident(n) = receiver.as_ref() {
                                        self.chan_ptr_elems.get(n).cloned()
                                    } else {
                                        None
                                    }
                                })
                                .unwrap_or_else(|| "int64_t".into());
                            let cname = self
                                .structs
                                .get(&st)
                                .map(|s| s.c_name.clone())
                                .unwrap_or(st);
                            let boxn = self.fresh("sbox");
                            self.line(&format!(
                                "{cname} *{boxn} = ({cname}*)malloc(sizeof({cname}));"
                            ));
                            self.line(&format!("*{boxn} = {v};"));
                            self.line(&format!(
                                "bool {tmp} = mako_chan_ptr_send({rv}, {boxn}) != 0;"
                            ));
                        } else {
                            let is_f64 = vty == "double"
                                || matches!(receiver.as_ref(), Expr::Ident(n) if self.chan_float.contains(n))
                                || self.chan_float.contains(&rv);
                            if is_f64 {
                                self.line(&format!(
                                    "bool {tmp} = mako_chan_send({rv}, mako_f64_to_bits({v})) != 0;"
                                ));
                            } else {
                                self.line(&format!(
                                    "bool {tmp} = mako_chan_send({rv}, {v}) != 0;"
                                ));
                            }
                        }
                        ("bool".into(), tmp)
                    }
                    "recv" => {
                        let tmp = self.fresh("rv");
                        if rty == "MakoChanStr*" {
                            self.line(&format!("MakoString {tmp} = mako_chan_str_recv({rv});"));
                            ("MakoString".into(), tmp)
                        } else if rty == "MakoChanPtr*" {
                            let st = self
                                .chan_ptr_elems
                                .get(&rv)
                                .cloned()
                                .or_else(|| {
                                    if let Expr::Ident(n) = receiver.as_ref() {
                                        self.chan_ptr_elems.get(n).cloned()
                                    } else {
                                        None
                                    }
                                })
                                .unwrap_or_else(|| "int64_t".into());
                            let cname = self
                                .structs
                                .get(&st)
                                .map(|s| s.c_name.clone())
                                .unwrap_or(st.clone());
                            let ptr = self.fresh("pp");
                            self.line(&format!(
                                "{cname} *{ptr} = ({cname}*)mako_chan_ptr_recv({rv});"
                            ));
                            // Zero-init fallback if closed empty
                            self.line(&format!("{cname} {tmp};"));
                            self.line(&format!(
                                "if ({ptr}) {{ {tmp} = *{ptr}; free({ptr}); }} else {{ memset(&{tmp}, 0, sizeof({tmp})); }}"
                            ));
                            (cname, tmp)
                        } else {
                            let is_f64 = matches!(receiver.as_ref(), Expr::Ident(n) if self.chan_float.contains(n))
                                || self.chan_float.contains(&rv);
                            if is_f64 {
                                let bits = self.fresh("bits");
                                self.line(&format!("int64_t {bits} = mako_chan_recv({rv});"));
                                self.line(&format!("double {tmp} = mako_bits_to_f64({bits});"));
                                ("double".into(), tmp)
                            } else {
                                self.line(&format!("int64_t {tmp} = mako_chan_recv({rv});"));
                                ("int64_t".into(), tmp)
                            }
                        }
                    }
                    "close" => {
                        if rty == "MakoChanStr*" {
                            self.line(&format!("mako_chan_str_close({rv});"));
                        } else if rty == "MakoChanPtr*" {
                            self.line(&format!("mako_chan_ptr_close({rv});"));
                        } else {
                            self.line(&format!("mako_chan_close({rv});"));
                        }
                        ("void".into(), "/*void*/".into())
                    }
                    "cancel" => {
                        self.line(&format!("mako_nursery_cancel(&{rv});"));
                        ("void".into(), "/*void*/".into())
                    }
                    "drain" => {
                        let (_, ms) = self.emit_expr(&args[0]);
                        let tmp = self.fresh("cdr");
                        self.line(&format!(
                            "int64_t {tmp} = mako_crew_drain(&{rv}, {ms});"
                        ));
                        ("int64_t".into(), tmp)
                    }
                    "cancelled" => (
                        "bool".into(),
                        format!("(mako_nursery_cancelled(&{rv}) != 0)"),
                    ),
                    "join" => {
                        let ret_ty = match receiver.as_ref() {
                            Expr::Ident(n) => self.job_rets.get(n).cloned(),
                            _ => self.job_rets.get(&rv).cloned(),
                        }
                        .unwrap_or_else(|| "int64_t".into());
                        self.emit_job_join(&rv, &ret_ty)
                    }
                    "join_timeout" => {
                        let (_, ms) = self.emit_expr(&args[0]);
                        let ret_ty = match receiver.as_ref() {
                            Expr::Ident(n) => self.job_rets.get(n).cloned(),
                            _ => self.job_rets.get(&rv).cloned(),
                        }
                        .unwrap_or_else(|| "int64_t".into());
                        // Always MakoResultInt: Ok(payload) or Err("timeout").
                        let out = self.fresh("jto");
                        let ok = self.fresh("jok");
                        let tmp = self.fresh("jtr");
                        self.line(&format!("int64_t {out} = 0;"));
                        self.line(&format!(
                            "int64_t {ok} = mako_await_timeout_ms({rv}, {ms}, &{out});"
                        ));
                        self.line(&format!("MakoResultInt {tmp};"));
                        if ret_ty == "MakoString" {
                            self.line(&format!(
                                "if ({ok}) {{ MakoString *p = (MakoString*)(intptr_t){out}; \
                                 if (p) {{ {tmp} = mako_ok_str(*p); free(p); }} else {{ {tmp} = mako_ok_str(mako_str_from_cstr(\"\")); }} }} \
                                 else {{ {tmp} = mako_err_int(mako_str_from_cstr(\"timeout\")); }}"
                            ));
                        } else if ret_ty == "MakoResultInt" {
                            // Nest: Ok(inner Result) packs via value pointer? Keep simple:
                            // on success return the job Result as the outer Result itself
                            // (flatten) so callers match Ok/Err for job outcome; on timeout Err.
                            self.line(&format!(
                                "if ({ok}) {{ MakoResultInt *p = (MakoResultInt*)(intptr_t){out}; \
                                 if (p) {{ {tmp} = *p; free(p); }} else {{ memset(&{tmp}, 0, sizeof({tmp})); {tmp}.ok = false; }} }} \
                                 else {{ {tmp} = mako_err_int(mako_str_from_cstr(\"timeout\")); }}"
                            ));
                        } else if ret_ty == "double" {
                            self.line(&format!(
                                "if ({ok}) {{ {tmp} = mako_ok_float_res(mako_bits_to_f64({out})); }} \
                                 else {{ {tmp} = mako_err_int(mako_str_from_cstr(\"timeout\")); }}"
                            ));
                        } else {
                            self.line(&format!(
                                "if ({ok}) {{ {tmp} = mako_ok_int({out}); }} \
                                 else {{ {tmp} = mako_err_int(mako_str_from_cstr(\"timeout\")); }}"
                            ));
                        }
                        let _ = rty;
                        ("MakoResultInt".into(), tmp)
                    }
                    "len" => {
                        // Prefer Type_len user method (Go-style receivers) over builtin.
                        let user_len = {
                            let tname = self
                                .structs
                                .iter()
                                .find(|(n, info)| *n == &rty || info.c_name == rty)
                                .map(|(n, _)| n.clone())
                                .or_else(|| {
                                    if self.structs.contains_key(&rty) {
                                        Some(rty.clone())
                                    } else {
                                        None
                                    }
                                });
                            tname.and_then(|tn| {
                                let k = format!("{tn}_len");
                                if self.fn_rets.contains_key(&k) {
                                    Some(k)
                                } else {
                                    None
                                }
                            })
                        };
                        if let Some(key) = user_len {
                            let call = format!("{}({})", mangle(&key), rv);
                            let ret = self
                                .fn_rets
                                .get(&key)
                                .cloned()
                                .unwrap_or_else(|| "int64_t".into());
                            let tmp = self.fresh("ul");
                            self.line(&format!("{ret} {tmp} = {call};"));
                            (ret, tmp)
                        } else if rty == "MakoString" {
                            ("int64_t".into(), format!("mako_str_len({rv})"))
                        } else if rty == "MakoFloatArray" {
                            ("int64_t".into(), format!("mako_float_array_len({rv})"))
                        } else if rty == "MakoStrArray" {
                            ("int64_t".into(), format!("mako_str_array_len({rv})"))
                        } else if rty == "MakoByteArray" {
                            ("int64_t".into(), format!("mako_byte_array_len({rv})"))
                        } else if rty == "MakoIntArray"
                            || rty.starts_with("MakoArr_")
                        {
                            ("int64_t".into(), format!("mako_array_len({rv})"))
                        } else {
                            // Unknown .len — try Type_len by rty name
                            let key = format!("{rty}_len");
                            if self.fn_rets.contains_key(&key) {
                                let call = format!("{}({})", mangle(&key), rv);
                                let ret = self
                                    .fn_rets
                                    .get(&key)
                                    .cloned()
                                    .unwrap_or_else(|| "int64_t".into());
                                let tmp = self.fresh("ul");
                                self.line(&format!("{ret} {tmp} = {call};"));
                                (ret, tmp)
                            } else {
                                ("int64_t".into(), format!("mako_array_len({rv})"))
                            }
                        }
                    }
                    other => {
                        // Struct/enum associated method: Type_method(self, ...)
                        // Includes `on Type { fn method… }` desugar.
                        {
                            let tname = self
                                .enums
                                .iter()
                                .find(|(n, info)| *n == &rty || info.c_name == rty)
                                .map(|(n, _)| n.clone())
                                .or_else(|| {
                                    self.structs
                                        .iter()
                                        .find(|(n, info)| *n == &rty || info.c_name == rty)
                                        .map(|(n, _)| n.clone())
                                })
                                .or_else(|| {
                                    // Receiver C type often equals struct name
                                    if self.structs.contains_key(&rty) {
                                        Some(rty.clone())
                                    } else {
                                        None
                                    }
                                });
                            let key = if let Some(ref tn) = tname {
                                let k = format!("{tn}_{other}");
                                if self.fn_rets.contains_key(&k) {
                                    k
                                } else {
                                    String::new()
                                }
                            } else {
                                String::new()
                            };
                            if !key.is_empty() {
                                let mut arg_vals = Vec::new();
                                for a in args {
                                    let (_, v) = self.emit_expr(a);
                                    arg_vals.push(v);
                                }
                                let params = self.fn_params.get(&key).cloned().unwrap_or_default();
                                let call_args = if params.len() == arg_vals.len() + 1 {
                                    let mut all = vec![rv.clone()];
                                    all.extend(arg_vals);
                                    all
                                } else {
                                    arg_vals
                                };
                                let call = format!("{}({})", mangle(&key), call_args.join(", "));
                                let ret = self
                                    .fn_rets
                                    .get(&key)
                                    .cloned()
                                    .unwrap_or_else(|| "int64_t".into());
                                if ret == "void" {
                                    self.line(&format!("{call};"));
                                    return ("void".into(), "/*void*/".into());
                                }
                                let tmp = self.fresh("em");
                                self.line(&format!("{ret} {tmp} = {call};"));
                                return (ret, tmp);
                            }
                        }
                        // Fat-pointer interface: recv.method(args) → recv.vtable->method(recv.data, ...)
                        if let Some(iname_prefix) = rty.strip_prefix("MakoIface_") {
                            let mut arg_vals = Vec::new();
                            for a in args {
                                let (_, v) = self.emit_expr(a);
                                arg_vals.push(v);
                            }
                            let mut call_args = vec![format!("{rv}.data")];
                            call_args.extend(arg_vals);
                            let call = format!("{rv}.vtable->{other}({})", call_args.join(", "));
                            let key = format!("{iname_prefix}_{other}");
                            let ret = self
                                .fn_rets
                                .get(&key)
                                .cloned()
                                .unwrap_or_else(|| "int64_t".into());
                            if ret == "void" {
                                self.line(&format!("{call};"));
                                return ("void".into(), "/*void*/".into());
                            }
                            let tmp = self.fresh("idyn");
                            self.line(&format!("{ret} {tmp} = {call};"));
                            return (ret, tmp);
                        }
                        // Interface method sugar: recv.method(args) → Iface_method([self,] args)
                        // or Iface_Concrete_method when multi-concrete.
                        let mut arg_vals = Vec::new();
                        for a in args {
                            let (_, v) = self.emit_expr(a);
                            arg_vals.push(v);
                        }
                        let mut resolved: Option<String> = None;
                        let recv_is_struct = self.structs.contains_key(&rty)
                            || self.structs.values().any(|s| s.c_name == rty);
                        let concrete_name = if self.structs.contains_key(&rty) {
                            Some(rty.clone())
                        } else if self.structs.values().any(|s| s.c_name == rty) {
                            Some(rty.clone())
                        } else {
                            None
                        };
                        for (iname, methods) in &self.interfaces {
                            if methods.iter().any(|m| m == other) {
                                if let Some(ref cn) = concrete_name {
                                    let alt = format!("{iname}_{cn}_{other}");
                                    if self.fn_rets.contains_key(&alt) {
                                        resolved = Some(alt);
                                        break;
                                    }
                                }
                                let key = format!("{iname}_{other}");
                                if self.fn_rets.contains_key(&key) {
                                    resolved = Some(key);
                                    break;
                                }
                            }
                        }
                        let _ = recv_is_struct;
                        if let Some(key) = resolved {
                            let params = self.fn_params.get(&key).cloned().unwrap_or_default();
                            let call_args = if params.len() == arg_vals.len() + 1 {
                                let mut all = vec![rv.clone()];
                                all.extend(arg_vals);
                                all
                            } else {
                                let _ = rv;
                                arg_vals
                            };
                            let call = format!("{}({})", mangle(&key), call_args.join(", "));
                            let ret = self
                                .fn_rets
                                .get(&key)
                                .cloned()
                                .unwrap_or_else(|| "int64_t".into());
                            let _ = rty;
                            if ret == "void" {
                                self.line(&format!("{call};"));
                                ("void".into(), "/*void*/".into())
                            } else {
                                let tmp = self.fresh("im");
                                self.line(&format!("{ret} {tmp} = {call};"));
                                (ret, tmp)
                            }
                        } else {
                            let _ = rty;
                            let _ = rv;
                            ("int64_t".into(), "0".into())
                        }
                    }
                }
            }
            Expr::Fan { collection, mapper } => {
                let (cty, coll) = self.emit_expr(collection);
                let map_fn = self.fresh("mapfn");
                if let Expr::Lambda { params, body } = mapper.as_ref() {
                    let p = params.first().cloned().unwrap_or_else(|| "x".into());
                    let helper = format!("__fan_{map_fn}");
                    let body_c = self.expr_as_pure_c(body, &p);
                    let tmp = self.fresh("pm");
                    if cty == "MakoFloatArray" {
                        let helper_src = format!(
                            "static double {helper}(double {p}) {{ return (double)({body_c}); }}\n"
                        );
                        self.insert_helper(&helper_src);
                        self.line(&format!(
                            "MakoFloatArray {tmp} = mako_par_map_float({coll}, {helper});"
                        ));
                        return ("MakoFloatArray".into(), tmp);
                    }
                    if cty == "MakoStrArray" {
                        let helper_src = format!(
                            "static MakoString {helper}(MakoString {p}) {{ return {body_c}; }}\n"
                        );
                        self.insert_helper(&helper_src);
                        self.line(&format!(
                            "MakoStrArray {tmp} = mako_par_map_str({coll}, {helper});"
                        ));
                        return ("MakoStrArray".into(), tmp);
                    }
                    // Struct array: MakoArr_Name
                    if let Some(sn) = cty.strip_prefix("MakoArr_") {
                        let map_one = format!("{helper}_one");
                        let helper_src = format!(
                            "static void {map_one}(void *dst, const void *src) {{\n\
                             \t{sn} {p} = *({sn} const *)src;\n\
                             \t*({sn} *)dst = ({body_c});\n\
                             }}\n"
                        );
                        self.insert_helper(&helper_src);
                        self.line(&format!("{cty} {tmp} = mako_arr_{sn}_make({coll}.len, 0);"));
                        self.line(&format!(
                            "mako_par_map_bytes({coll}.data, {tmp}.data, (size_t){coll}.len, sizeof({sn}), {map_one});"
                        ));
                        return (cty, tmp);
                    }
                    let helper_src =
                        format!("static int64_t {helper}(int64_t {p}) {{ return {body_c}; }}\n");
                    self.insert_helper(&helper_src);
                    self.line(&format!(
                        "MakoIntArray {tmp} = mako_par_map_int({coll}, {helper});"
                    ));
                    return ("MakoIntArray".into(), tmp);
                }
                (cty, coll)
            }
            Expr::Block(b) => {
                self.line("{");
                self.indent += 1;
                self.push_share_scope();
                for s in &b.stmts {
                    self.emit_stmt(s);
                }
                self.pop_share_scope();
                self.indent -= 1;
                self.line("}");
                ("void".into(), "/*void*/".into())
            }
            Expr::Lambda { .. } => ("/*fn*/".into(), "NULL".into()),
            Expr::Match { scrutinee, arms } => self.emit_match(scrutinee, arms),
            Expr::IfExpr {
                cond,
                then_block,
                else_block,
            } => self.emit_if_expr(cond, then_block, else_block),
            Expr::Field { base, field } => {
                // (handled below — pure-c helper also supports fields)
                let (bty, b) = self.emit_expr(base);
                if let Some(info) = self.structs.get(&bty) {
                    let fty = info
                        .fields
                        .iter()
                        .find(|(n, _)| n == field)
                        .map(|(_, t)| t.clone())
                        .unwrap_or_else(|| "int64_t".into());
                    return (fty, format!("{b}.{field}"));
                }
                ("int64_t".into(), format!("{b}.{field}"))
            }
        }
    }

    fn emit_enum_construct(
        &mut self,
        enum_name: &str,
        variant: &str,
        args: &[Expr],
    ) -> (String, String) {
        let info = self.enums.get(enum_name).cloned().unwrap();
        let (tag, fields) = info.variants.get(variant).cloned().unwrap();
        let tmp = self.fresh("ev");
        self.line(&format!("{} {tmp};", info.c_name));
        self.line(&format!("{tmp}.tag = {tag};"));
        self.line(&format!("{tmp}.i0 = 0;"));
        self.line(&format!("{tmp}.i1 = 0;"));
        self.line(&format!("{tmp}.i2 = 0;"));
        self.line(&format!("{tmp}.s0 = (MakoString){{NULL, 0}};"));
        self.line(&format!("{tmp}.s1 = (MakoString){{NULL, 0}};"));
        let mut int_slot = 0usize;
        let mut str_slot = 0usize;
        for (i, a) in args.iter().enumerate() {
            let (aty, v) = self.emit_expr(a);
            let kind = fields.get(i).copied().unwrap_or("int");
            match kind {
                "string" => {
                    if str_slot < 2 {
                        self.line(&format!("{tmp}.s{str_slot} = {v};"));
                        str_slot += 1;
                    }
                }
                "float" => {
                    if int_slot < 3 {
                        self.line(&format!(
                            "{tmp}.i{int_slot} = mako_f64_to_bits((double)({v}));"
                        ));
                        int_slot += 1;
                    }
                    let _ = aty;
                }
                _ => {
                    if int_slot < 3 {
                        self.line(&format!("{tmp}.i{int_slot} = (int64_t){v};"));
                        int_slot += 1;
                    }
                }
            }
        }
        (info.c_name, tmp)
    }

    /// Emit a block and return the value of its trailing expression (or void if
    /// the block does not end in an expression). Non-trailing statements are
    /// emitted normally.
    fn emit_block_trailing(&mut self, block: &Block) -> (String, String) {
        let n = block.stmts.len();
        for (i, stmt) in block.stmts.iter().enumerate() {
            if i + 1 == n {
                match stmt {
                    Stmt::Expr(e) => return self.emit_expr(e),
                    // A trailing `if … else …` statement is itself the block value.
                    Stmt::If {
                        init: None,
                        cond,
                        then_block,
                        else_block: Some(eb),
                    } => return self.emit_if_expr(cond, then_block, eb),
                    _ => {}
                }
            }
            self.emit_stmt(stmt);
        }
        ("void".into(), "/*void*/".into())
    }

    fn emit_if_expr(
        &mut self,
        cond: &Expr,
        then_block: &Block,
        else_block: &Block,
    ) -> (String, String) {
        let result = self.fresh("ife");
        let (_, c) = self.emit_expr(cond);
        // Declare `result` before the `if`; its type is filled in once a branch's
        // value type is known (marker replaced below).
        let marker = format!("/*__IFE_DECL_{result}__*/");
        self.line(&marker);
        self.line(&format!("if ({c}) {{"));
        self.indent += 1;
        self.push_share_scope();
        let (tty, tval) = self.emit_block_trailing(then_block);
        if tval != "/*void*/" {
            self.line(&format!("{result} = {tval};"));
        }
        self.pop_share_scope();
        self.indent -= 1;
        self.line("} else {");
        self.indent += 1;
        self.push_share_scope();
        let (ety, eval) = self.emit_block_trailing(else_block);
        if eval != "/*void*/" {
            self.line(&format!("{result} = {eval};"));
        }
        self.pop_share_scope();
        self.indent -= 1;
        self.line("}");
        let ty = if tty != "void" {
            tty
        } else if ety != "void" {
            ety
        } else {
            "int64_t".into()
        };
        let decl = if ty == "void" {
            format!("int64_t {result} = 0; /* void if-expr */")
        } else {
            format!("{ty} {result};")
        };
        if let Some(pos) = self.out.find(&marker) {
            self.out.replace_range(pos..pos + marker.len(), &decl);
        }
        (ty, result)
    }

    fn emit_match(&mut self, scrutinee: &Expr, arms: &[MatchArm]) -> (String, String) {
        let (sty, sval) = self.emit_expr(scrutinee);
        let result = self.fresh("m");
        // Infer result C type from first arm by emitting into a probe — use int64_t default,
        // upgrade when we see string/result/option/enum from arm bodies.
        // Two-pass: emit arms into a block assigning to result; declare result after first arm type.
        // Simpler: always use a union-like approach — declare after computing first arm.
        let mut result_ty: Option<String> = None;
        let scrut = self.fresh("scrut");
        self.line(&format!("{sty} {scrut} = {sval};"));
        // Propagate Result Err enum type from local/ident, call target, or current fn return.
        if sty == "MakoResultInt" {
            let err_c = match scrutinee {
                Expr::Ident(n) => self.result_err_enums.get(n).cloned(),
                Expr::Call { callee, .. } => {
                    if let Expr::Ident(fname) = callee.as_ref() {
                        self.fn_result_err_enum.get(fname).cloned()
                    } else {
                        None
                    }
                }
                _ => None,
            }
            .or_else(|| self.current_result_err_enum.clone());
            if let Some(ec) = err_c {
                self.result_err_enums.insert(scrut.clone(), ec);
            }
            let ok_c = match scrutinee {
                Expr::Ident(n) => self.result_ok_kinds.get(n).cloned(),
                Expr::Call { callee, .. } => {
                    if let Expr::Ident(fname) = callee.as_ref() {
                        self.fn_result_ok_kind.get(fname).cloned()
                    } else {
                        None
                    }
                }
                Expr::Join(inner) => match inner.as_ref() {
                    Expr::Ident(n) => self.job_ok_kinds.get(n).cloned(),
                    _ => self.result_ok_kinds.get(&sval).cloned(),
                },
                Expr::Method {
                    receiver,
                    method,
                    ..
                } if method == "join" => match receiver.as_ref() {
                    Expr::Ident(n) => self.job_ok_kinds.get(n).cloned(),
                    _ => self.result_ok_kinds.get(&sval).cloned(),
                },
                _ => self.result_ok_kinds.get(&sval).cloned(),
            };
            if let Some(ok) = ok_c {
                self.result_ok_kinds.insert(scrut.clone(), ok);
            }
            let ok_st = match scrutinee {
                Expr::Ident(n) => self.result_ok_structs.get(n).cloned(),
                Expr::Call { callee, .. } => {
                    if let Expr::Ident(fname) = callee.as_ref() {
                        self.fn_result_ok_struct.get(fname).cloned()
                    } else {
                        None
                    }
                }
                _ => self.result_ok_structs.get(&sval).cloned(),
            };
            if let Some(sn) = ok_st {
                self.result_ok_structs.insert(scrut.clone(), sn);
            }
        }

        let marker = format!("/*__MATCH_DECL_{result}__*/");
        self.line(&marker);

        let mut first = true;
        for arm in arms {
            let cond = self.pattern_condition(&scrut, &sty, &arm.pattern);
            if first {
                self.line(&format!("if ({cond}) {{"));
                first = false;
            } else {
                self.line(&format!("}} else if ({cond}) {{"));
            }
            self.indent += 1;
            self.bind_pattern_locals(&scrut, &sty, &arm.pattern);
            let (bty, bval) = self.emit_expr(&arm.body);
            if result_ty.is_none() {
                result_ty = Some(bty.clone());
            }
            if bval != "/*void*/" {
                self.line(&format!("{result} = {bval};"));
            }
            self.indent -= 1;
        }
        self.line("} else {");
        self.indent += 1;
        self.line("fprintf(stderr, \"non-exhaustive match\\n\"); abort();");
        self.indent -= 1;
        self.line("}");

        let ty = result_ty.unwrap_or_else(|| "int64_t".into());
        // `void` is not a storable C type — statement-only matches use a dummy int.
        let (ty, decl) = if ty == "void" {
            (
                "int64_t".into(),
                format!("int64_t {result} = 0; /* void match */"),
            )
        } else {
            (ty.clone(), format!("{ty} {result};"))
        };
        if let Some(pos) = self.out.find(&marker) {
            self.out.replace_range(pos..pos + marker.len(), &decl);
        }
        (ty, result)
    }

    fn pattern_condition(&self, scrut: &str, sty: &str, pattern: &Pattern) -> String {
        match pattern {
            Pattern::Wildcard => "1".into(),
            Pattern::Ident(n) if n == "None" => format!("(!{scrut}.some)"),
            Pattern::Ident(n) => {
                // Unit enum variant?
                if let Some(enum_name) = self.variant_to_enum.get(n) {
                    if let Some(info) = self.enums.get(enum_name) {
                        if let Some((tag, fields)) = info.variants.get(n) {
                            if fields.is_empty() {
                                return format!("{scrut}.tag == {tag}");
                            }
                        }
                    }
                }
                "1".into()
            }
            Pattern::Literal(Expr::Bool(b)) => {
                if *b {
                    format!("({scrut})")
                } else {
                    format!("(!{scrut})")
                }
            }
            Pattern::Literal(Expr::Int(n)) => format!("({scrut} == {n})"),
            Pattern::Literal(Expr::String(s)) => {
                format!(
                    "mako_str_eq({scrut}, mako_str_from_cstr(\"{}\"))",
                    escape_c(s)
                )
            }
            Pattern::Variant { name, .. } => {
                if sty == "MakoResultInt" {
                    if name == "Ok" {
                        format!("({scrut}.ok)")
                    } else {
                        format!("(!{scrut}.ok)")
                    }
                } else if sty == "MakoOptionInt" {
                    if name == "Some" {
                        format!("({scrut}.some)")
                    } else {
                        format!("(!{scrut}.some)")
                    }
                } else if let Some(enum_name) = self.variant_to_enum.get(name) {
                    let tag = self.enums[enum_name].variants[name].0;
                    format!("{scrut}.tag == {tag}")
                } else {
                    "0".into()
                }
            }
            Pattern::Literal(_) => "0".into(),
            Pattern::Or(alts) => {
                let parts: Vec<String> = alts
                    .iter()
                    .map(|p| self.pattern_condition(scrut, sty, p))
                    .collect();
                format!("({})", parts.join(" || "))
            }
            Pattern::Tuple(_) => "1".into(), // irrefutable when types match
        }
    }

    fn bind_pattern_locals(&mut self, scrut: &str, sty: &str, pattern: &Pattern) {
        match pattern {
            Pattern::Or(_) => {}
            Pattern::Tuple(parts) => {
                // sty like MakoTup_int_string → field C types
                let tags: Vec<&str> = sty
                    .strip_prefix("MakoTup_")
                    .unwrap_or("")
                    .split('_')
                    .collect();
                for (i, p) in parts.iter().enumerate() {
                    if let Pattern::Ident(n) = p {
                        if n == "_" {
                            continue;
                        }
                        let tag = tags.get(i).copied().unwrap_or("int");
                        let cty = match tag {
                            "string" => "MakoString",
                            "float" => "double",
                            "bool" => "bool",
                            _ => "int64_t",
                        };
                        self.locals.insert(n.clone(), cty.into());
                        self.line(&format!("{cty} {n} = {scrut}._{i};"));
                    }
                }
            }
            Pattern::Ident(n) if n != "None" && n != "_" => {
                // Unit variant: no binding
                if let Some(enum_name) = self.variant_to_enum.get(n) {
                    if let Some(info) = self.enums.get(enum_name) {
                        if let Some((_, fields)) = info.variants.get(n) {
                            if fields.is_empty() {
                                return;
                            }
                        }
                    }
                }
                self.locals.insert(n.clone(), sty.to_string());
                self.line(&format!("{sty} {} = {scrut};", mangle(n)));
            }
            Pattern::Variant { name, bindings } => {
                if sty == "MakoResultInt" {
                    if name == "Ok" && bindings.len() == 1 {
                        let ok_kind = self
                            .result_ok_kinds
                            .get(scrut)
                            .cloned()
                            .unwrap_or_else(|| "int".into());
                        if ok_kind == "string" {
                            self.locals.insert(bindings[0].clone(), "MakoString".into());
                            self.line(&format!(
                                "MakoString {} = {scrut}.ok_s;",
                                mangle(&bindings[0])
                            ));
                        } else if ok_kind == "float" {
                            self.locals.insert(bindings[0].clone(), "double".into());
                            self.line(&format!(
                                "double {} = {scrut}.ok_f;",
                                mangle(&bindings[0])
                            ));
                        } else if ok_kind == "struct" {
                            let sn = self
                                .result_ok_structs
                                .get(scrut)
                                .cloned()
                                .unwrap_or_else(|| "int64_t".into());
                            let cname = self
                                .structs
                                .get(&sn)
                                .map(|s| s.c_name.clone())
                                .unwrap_or(sn);
                            let b = mangle(&bindings[0]);
                            let p = self.fresh("okp");
                            self.locals.insert(bindings[0].clone(), cname.clone());
                            self.line(&format!(
                                "{cname} *{p} = ({cname}*)mako_result_ok_ptr({scrut});"
                            ));
                            self.line(&format!("{cname} {b};"));
                            self.line(&format!(
                                "if ({p}) {{ {b} = *{p}; free({p}); }} else {{ memset(&{b}, 0, sizeof({b})); }}"
                            ));
                        } else if ok_kind == "slice" {
                            let b = mangle(&bindings[0]);
                            let p = self.fresh("oks");
                            self.locals.insert(bindings[0].clone(), "MakoIntArray".into());
                            self.line(&format!(
                                "MakoIntArray *{p} = (MakoIntArray*)mako_result_ok_ptr({scrut});"
                            ));
                            self.line(&format!("MakoIntArray {b};"));
                            self.line(&format!(
                                "if ({p}) {{ {b} = *{p}; free({p}); }} else {{ {b}.data = NULL; {b}.len = 0; {b}.cap = 0; }}"
                            ));
                        } else if ok_kind == "map" || ok_kind == "map_si" {
                            let b = mangle(&bindings[0]);
                            self.locals.insert(bindings[0].clone(), "MakoMapSI*".into());
                            self.line(&format!(
                                "MakoMapSI *{b} = (MakoMapSI*)mako_result_ok_ptr({scrut});"
                            ));
                        } else if ok_kind == "map_ii" {
                            let b = mangle(&bindings[0]);
                            self.locals.insert(bindings[0].clone(), "MakoMapII*".into());
                            self.line(&format!(
                                "MakoMapII *{b} = (MakoMapII*)mako_result_ok_ptr({scrut});"
                            ));
                        } else if ok_kind == "map_ss" {
                            let b = mangle(&bindings[0]);
                            self.locals.insert(bindings[0].clone(), "MakoMapSS*".into());
                            self.line(&format!(
                                "MakoMapSS *{b} = (MakoMapSS*)mako_result_ok_ptr({scrut});"
                            ));
                        } else {
                            self.locals.insert(bindings[0].clone(), "int64_t".into());
                            self.line(&format!(
                                "int64_t {} = {scrut}.value;",
                                mangle(&bindings[0])
                            ));
                        }
                    } else if name == "Err" && bindings.len() == 1 {
                        let b = mangle(&bindings[0]);
                        if let Some(cty) = self.result_err_enums.get(scrut).cloned() {
                            // Enum Err: reconstruct full enum value from packed fields.
                            self.locals.insert(bindings[0].clone(), cty.clone());
                            self.line(&format!(
                                "{cty} {b}; \
                                 {b}.tag = {scrut}.err_tag; \
                                 {b}.i0 = {scrut}.err_i0; \
                                 {b}.i1 = {scrut}.err_i1; \
                                 {b}.i2 = {scrut}.err_i2; \
                                 {b}.s0 = {scrut}.err_s0; \
                                 {b}.s1 = {scrut}.err_s1;"
                            ));
                        } else {
                            self.locals.insert(bindings[0].clone(), "MakoString".into());
                            self.line(&format!("MakoString {b} = {scrut}.err;"));
                        }
                    }
                } else if sty == "MakoOptionInt" {
                    if name == "Some" && bindings.len() == 1 {
                        self.locals.insert(bindings[0].clone(), "int64_t".into());
                        self.line(&format!("int64_t {} = {scrut}.value;", mangle(&bindings[0])));
                    }
                } else if let Some(enum_name) = self.variant_to_enum.get(name).cloned() {
                    let fields = self.enums[&enum_name].variants[name].1.clone();
                    let mut int_slot = 0usize;
                    let mut str_slot = 0usize;
                    for (i, b) in bindings.iter().enumerate() {
                        let kind = fields.get(i).copied().unwrap_or("int");
                        match kind {
                            "string" => {
                                self.locals.insert(b.clone(), "MakoString".into());
                                self.line(&format!("MakoString {} = {scrut}.s{str_slot};", mangle(b)));
                                str_slot += 1;
                            }
                            "bool" => {
                                self.locals.insert(b.clone(), "bool".into());
                                self.line(&format!("bool {} = (bool){scrut}.i{int_slot};", mangle(b)));
                                int_slot += 1;
                            }
                            _ => {
                                self.locals.insert(b.clone(), "int64_t".into());
                                self.line(&format!("int64_t {} = {scrut}.i{int_slot};", mangle(b)));
                                int_slot += 1;
                            }
                        }
                    }
                }
            }
            _ => {}
        }
    }

    fn insert_helper(&mut self, helper_src: &str) {
        const MARKER: &str = "/*__MAKO_HELPERS__*/\n";
        if let Some(pos) = self.out.find(MARKER) {
            let insert_at = pos + MARKER.len();
            self.out.insert_str(insert_at, helper_src);
        } else {
            self.out.push_str(helper_src);
        }
    }

    fn emit_spawn_helper_typed(&mut self, helper: &str, fname: &str, param_tys: &[String]) {
        if param_tys.is_empty() {
            let ret_ty = self
                .fn_rets
                .get(fname)
                .map(|s| s.as_str())
                .unwrap_or("int64_t");
            let call = format!("{}()", mangle(fname));
            let body = if ret_ty == "MakoString" {
                format!(
                    "MakoString __r = {call};\n\
                     MakoString *__box = (MakoString*)malloc(sizeof(MakoString));\n\
                     *__box = __r;\nreturn (void*)__box;\n"
                )
            } else if ret_ty == "MakoResultInt" {
                format!(
                    "MakoResultInt __r = {call};\n\
                     MakoResultInt *__box = (MakoResultInt*)malloc(sizeof(MakoResultInt));\n\
                     *__box = __r;\nreturn (void*)__box;\n"
                )
            } else if ret_ty == "double" {
                format!("return (void*)(intptr_t)mako_f64_to_bits({call});\n")
            } else if ret_ty.contains('*') {
                format!("return (void*){call};\n")
            } else {
                format!("return (void*)(intptr_t)(int64_t){call};\n")
            };
            let helper_src =
                format!("static void *{helper}(void *arg) {{ (void)arg;\n{body}}}\n");
            self.insert_helper(&helper_src);
            return;
        }
        let mut unpack = String::from("intptr_t *a = (intptr_t*)arg;\n");
        let mut call_args = Vec::new();
        let mut cleanup = String::new();
        for (i, ty) in param_tys.iter().enumerate() {
            let local = format!("p{i}");
            if ty == "MakoShareInt" {
                unpack.push_str(&format!(
                    "MakoShareInt {local} = *(MakoShareInt*)a[{i}]; free((void*)a[{i}]);\n"
                ));
                cleanup.push_str(&format!("mako_share_drop({local});\n"));
                call_args.push(local);
            } else if ty == "MakoString" {
                unpack.push_str(&format!(
                    "MakoString {local} = *(MakoString*)a[{i}]; free((void*)a[{i}]);\n"
                ));
                // callee owns the clone for the call duration; free buffer after if needed
                call_args.push(local);
            } else if ty == "double" {
                unpack.push_str(&format!(
                    "double {local} = mako_bits_to_f64((int64_t)a[{i}]);\n"
                ));
                call_args.push(local);
            } else if self.structs.contains_key(ty)
                || self.structs.values().any(|s| s.c_name == *ty)
            {
                unpack.push_str(&format!(
                    "{ty} {local} = *({ty}*)a[{i}]; free((void*)a[{i}]);\n"
                ));
                call_args.push(local);
            } else if ty.contains('*') {
                unpack.push_str(&format!("{ty} {local} = ({ty})a[{i}];\n"));
                call_args.push(local);
            } else {
                unpack.push_str(&format!("{ty} {local} = ({ty})a[{i}];\n"));
                call_args.push(local);
            }
        }
        let ret_ty = self
            .fn_rets
            .get(fname)
            .map(|s| s.as_str())
            .unwrap_or("int64_t");
        let call = format!("{}({})", mangle(fname), call_args.join(", "));
        let body = if ret_ty == "MakoString" {
            format!(
                "{unpack}MakoString __r = {call};\n\
                 MakoString *__box = (MakoString*)malloc(sizeof(MakoString));\n\
                 *__box = __r;\n{cleanup}return (void*)__box;\n"
            )
        } else if ret_ty == "MakoResultInt" {
            format!(
                "{unpack}MakoResultInt __r = {call};\n\
                 MakoResultInt *__box = (MakoResultInt*)malloc(sizeof(MakoResultInt));\n\
                 *__box = __r;\n{cleanup}return (void*)__box;\n"
            )
        } else if ret_ty.contains('*') {
            format!("{unpack}void *__r = (void*){call};\n{cleanup}return __r;\n")
        } else if ret_ty == "double" {
            format!(
                "{unpack}double __r = {call};\n\
                 return (void*)(intptr_t)mako_f64_to_bits(__r);\n"
            )
        } else {
            format!(
                "{unpack}int64_t __r = (int64_t){call};\n{cleanup}return (void*)(intptr_t)__r;\n"
            )
        };
        let helper_src = format!("static void *{helper}(void *arg) {{\n{body}}}\n");
        self.insert_helper(&helper_src);
    }

    /// Unbox `mako_await` result according to the kicked function's C return type.
    fn emit_job_join(&mut self, task: &str, ret_ty: &str) -> (String, String) {
        let tmp = self.fresh("jn");
        if ret_ty == "MakoString" {
            let p = self.fresh("jsp");
            self.line(&format!("MakoString *{p} = (MakoString*)mako_await({task});"));
            self.line(&format!("MakoString {tmp};"));
            self.line(&format!(
                "if ({p}) {{ {tmp} = *{p}; free({p}); }} else {{ {tmp} = mako_str_from_cstr(\"\"); }}"
            ));
            ("MakoString".into(), tmp)
        } else if ret_ty == "MakoResultInt" {
            let p = self.fresh("jrp");
            self.line(&format!(
                "MakoResultInt *{p} = (MakoResultInt*)mako_await({task});"
            ));
            self.line(&format!("MakoResultInt {tmp};"));
            self.line(&format!(
                "if ({p}) {{ {tmp} = *{p}; free({p}); }} else {{ memset(&{tmp}, 0, sizeof({tmp})); {tmp}.ok = false; }}"
            ));
            if let Some(ok) = self.job_ok_kinds.get(task).cloned() {
                self.result_ok_kinds.insert(tmp.clone(), ok);
            }
            ("MakoResultInt".into(), tmp)
        } else if ret_ty == "double" {
            self.line(&format!(
                "double {tmp} = mako_bits_to_f64((int64_t)(intptr_t)mako_await({task}));"
            ));
            ("double".into(), tmp)
        } else {
            self.line(&format!(
                "int64_t {tmp} = (int64_t)(intptr_t)mako_await({task});"
            ));
            ("int64_t".into(), tmp)
        }
    }

    fn expr_as_pure_c(&self, expr: &Expr, param: &str) -> String {
        match expr {
            Expr::Ident(n) if n == param => n.clone(),
            Expr::Ident(n) => mangle(n),
            Expr::Int(n) => n.to_string(),
            Expr::String(s) => format!("mako_str_from_cstr(\"{}\")", escape_c(s)),
            Expr::Float(n) => {
                // Ensure C double literal
                let s = n.to_string();
                if s.contains('.') || s.contains('e') || s.contains('E') {
                    s
                } else {
                    format!("{s}.0")
                }
            }
            Expr::Bool(b) => if *b { "1".into() } else { "0".into() },
            Expr::Binary { op, left, right } => {
                let l = self.expr_as_pure_c(left, param);
                let r = self.expr_as_pure_c(right, param);
                // String concat in pure fan mappers
                if matches!(op, BinOp::Add)
                    && (l.contains("MakoString")
                        || r.contains("MakoString")
                        || l.contains("mako_str")
                        || r.contains("mako_str")
                        || l == *param
                        || r == *param)
                {
                    // Prefer mako_str_concat when either side looks stringy; for pure
                    // string fan bodies `|x| x` just returns x (Ident).
                    if matches!(left.as_ref(), Expr::String(_))
                        || matches!(right.as_ref(), Expr::String(_))
                        || matches!(left.as_ref(), Expr::Ident(n) if n == param)
                        || matches!(right.as_ref(), Expr::Ident(n) if n == param)
                    {
                        // Only force concat when one side is a string literal
                        if matches!(left.as_ref(), Expr::String(_))
                            || matches!(right.as_ref(), Expr::String(_))
                        {
                            return format!("mako_str_concat({l}, {r})");
                        }
                    }
                }
                let cop = match op {
                    BinOp::Add => "+",
                    BinOp::Sub => "-",
                    BinOp::Mul => "*",
                    BinOp::Div => "/",
                    BinOp::Mod => "%",
                    BinOp::BitAnd => "&",
                    BinOp::BitOr => "|",
                    BinOp::BitXor => "^",
                    BinOp::Shl => "<<",
                    BinOp::Shr => ">>",
                    BinOp::BitClear => {
                        return format!("(({l}) & ~({r}))");
                    }
                    _ => "+",
                };
                format!("({l} {cop} {r})")
            }
            Expr::Unary { op, expr } => {
                let e = self.expr_as_pure_c(expr, param);
                match op {
                    UnaryOp::Neg => format!("(-{e})"),
                    UnaryOp::Not => format!("(!{e})"),
                    UnaryOp::BitNot => format!("(~{e})"),
                }
            }
            Expr::Field { base, field } => {
                let b = self.expr_as_pure_c(base, param);
                format!("{b}.{}", mangle(field))
            }
            Expr::StructLit { name, fields } => {
                let mut parts = Vec::new();
                for (fname, fexpr) in fields {
                    parts.push(format!(
                        ".{} = {}",
                        mangle(fname),
                        self.expr_as_pure_c(fexpr, param)
                    ));
                }
                format!("({name}){{ {} }}", parts.join(", "))
            }
            Expr::StructLitPos { name, values } => {
                // Positional: rely on declaration order field names from structs map.
                if let Some(info) = self.structs.get(name) {
                    let mut parts = Vec::new();
                    for (i, v) in values.iter().enumerate() {
                        if let Some((fname, _)) = info.fields.get(i) {
                            parts.push(format!(
                                ".{} = {}",
                                mangle(fname),
                                self.expr_as_pure_c(v, param)
                            ));
                        }
                    }
                    format!("({name}){{ {} }}", parts.join(", "))
                } else {
                    "0".into()
                }
            }
            Expr::Call { callee, args } => {
                if let Expr::Ident(name) = callee.as_ref() {
                    let as_: Vec<_> = args.iter().map(|a| self.expr_as_pure_c(a, param)).collect();
                    return format!("{}({})", mangle(name), as_.join(", "));
                }
                "0".into()
            }
            // `fn(x) { x * 2 }` / `fn(x) { return x * 2 }` — last expr/return is the map body
            Expr::Block(b) => {
                for s in b.stmts.iter().rev() {
                    match s {
                        Stmt::Return(Some(e)) | Stmt::Expr(e) => {
                            return self.expr_as_pure_c(e, param);
                        }
                        _ => {}
                    }
                }
                "0".into()
            }
            _ => "0".into(),
        }
    }
}

/// The struct name of a named or positional struct literal, if `e` is one.
fn struct_lit_name(e: &Expr) -> Option<&str> {
    match e {
        Expr::StructLit { name, .. } | Expr::StructLitPos { name, .. } => Some(name),
        _ => None,
    }
}

fn mangle(name: &str) -> String {
    if name == "main" {
        return "mako_main".into();
    }
    // Rename Mako identifiers that collide with C keywords or common C/POSIX
    // library symbols so the generated C stays valid and links (`fn read` →
    // `mk_read`). Applied uniformly at declaration and use sites so the mapping
    // stays consistent; `extern` functions bypass mangling and keep their real
    // symbol name.
    if is_c_reserved(name) || is_c_stdlib_name(name) {
        return format!("mk_{name}");
    }
    name.to_string()
}

/// Common C / POSIX library function names that are plausible Mako identifiers.
/// A user function or local with one of these names would otherwise clash with a
/// libc declaration (a real problem for backend code: `read`, `write`, `time`, …).
fn is_c_stdlib_name(name: &str) -> bool {
    matches!(
        name,
        // I/O and files
        "read" | "write" | "open" | "close" | "creat" | "lseek" | "pread" | "pwrite"
        | "remove" | "rename" | "unlink" | "link" | "symlink" | "readlink" | "access"
        | "stat" | "fstat" | "lstat" | "chmod" | "chown" | "truncate" | "ftruncate"
        | "mkdir" | "rmdir" | "chdir" | "getcwd" | "fsync" | "fdatasync" | "sync"
        | "dup" | "dup2" | "pipe" | "fcntl" | "ioctl" | "flock" | "umask"
        // process / signals
        | "fork" | "exec" | "execve" | "execvp" | "wait" | "waitpid" | "exit" | "_exit"
        | "abort" | "kill" | "raise" | "signal" | "alarm" | "pause" | "sleep" | "usleep"
        | "getpid" | "getppid" | "getuid" | "getgid" | "setuid" | "setgid" | "nice"
        | "system" | "atexit" | "getenv" | "setenv" | "putenv" | "unsetenv"
        // memory / strings
        | "malloc" | "calloc" | "realloc" | "free" | "memcpy" | "memmove" | "memset"
        | "memcmp" | "strcpy" | "strncpy" | "strcat" | "strcmp" | "strlen" | "strdup"
        | "strchr" | "strstr" | "strtok" | "index" | "rindex"
        // stdio
        | "printf" | "fprintf" | "sprintf" | "snprintf" | "scanf" | "sscanf"
        | "puts" | "gets" | "putchar" | "getchar" | "perror" | "fopen" | "fclose"
        | "fread" | "fwrite" | "fseek" | "ftell" | "fflush"
        // math
        | "abs" | "labs" | "div" | "ldiv" | "atoi" | "atol" | "atof" | "strtol"
        | "rand" | "srand" | "random" | "srandom" | "exp" | "log" | "log2" | "log10"
        | "pow" | "sqrt" | "cbrt" | "sin" | "cos" | "tan" | "floor" | "ceil"
        | "round" | "trunc" | "fabs" | "fmod" | "hypot"
        // sockets / net
        | "socket" | "bind" | "listen" | "accept" | "connect" | "send" | "recv"
        | "sendto" | "recvfrom" | "shutdown" | "select" | "poll" | "htons" | "ntohs"
        // time
        | "time" | "clock" | "mktime" | "gmtime" | "localtime" | "ctime" | "difftime"
        // sorting
        | "qsort" | "bsearch"
    )
}

/// C reserved words (C11 keywords plus a few common typedefs) that are legal Mako
/// identifiers and would otherwise produce invalid C when emitted verbatim.
fn is_c_reserved(name: &str) -> bool {
    matches!(
        name,
        "auto"
            | "break"
            | "case"
            | "char"
            | "const"
            | "continue"
            | "default"
            | "do"
            | "double"
            | "else"
            | "enum"
            | "extern"
            | "float"
            | "for"
            | "goto"
            | "if"
            | "inline"
            | "int"
            | "long"
            | "register"
            | "restrict"
            | "return"
            | "short"
            | "signed"
            | "sizeof"
            | "static"
            | "struct"
            | "switch"
            | "typedef"
            | "union"
            | "unsigned"
            | "void"
            | "volatile"
            | "while"
            | "_Bool"
            | "_Complex"
            | "_Atomic"
    )
}

fn type_expr_schema(t: &TypeExpr) -> String {
    match t {
        TypeExpr::Named(n) => n.clone(),
        TypeExpr::Array(inner) => format!("[]{}", type_expr_schema(inner)),
        TypeExpr::Map(k, v) => format!("map[{}]{}", type_expr_schema(k), type_expr_schema(v)),
        TypeExpr::Generic(n, args) => {
            let a: Vec<_> = args.iter().map(type_expr_schema).collect();
            format!("{n}<{}>", a.join(","))
        }
        TypeExpr::Fn(params, ret) => {
            let p: Vec<_> = params.iter().map(type_expr_schema).collect();
            format!("fn({})->{}", p.join(","), type_expr_schema(ret))
        }
        TypeExpr::Tuple(elems) => {
            let e: Vec<_> = elems.iter().map(type_expr_schema).collect();
            format!("({})", e.join(","))
        }
    }
}

fn escape_c(s: &str) -> String {
    let mut out = String::new();
    for c in s.chars() {
        match c {
            '\\' => out.push_str("\\\\"),
            '"' => out.push_str("\\\""),
            '\n' => out.push_str("\\n"),
            '\t' => out.push_str("\\t"),
            '\r' => out.push_str("\\r"),
            other => out.push(other),
        }
    }
    out
}


fn c_type_mono_tag(c_ty: &str) -> String {
    match c_ty {
        "int64_t" => "int".into(),
        "double" => "float".into(),
        "bool" => "bool".into(),
        "MakoString" => "string".into(),
        "MakoChan*" => "chan_int".into(),
        "MakoChanStr*" => "chan_string".into(),
        other => other
            .chars()
            .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
            .collect(),
    }
}

impl Codegen {
    fn predeclare_tuples(&mut self, program: &Program) {
        let mut tuples: Vec<Vec<TypeExpr>> = Vec::new();
        fn walk_ty(t: &TypeExpr, out: &mut Vec<Vec<TypeExpr>>) {
            if let TypeExpr::Tuple(elems) = t {
                out.push(elems.clone());
                for e in elems {
                    walk_ty(e, out);
                }
            }
            match t {
                TypeExpr::Array(i) => walk_ty(i, out),
                TypeExpr::Map(k, v) => {
                    walk_ty(k, out);
                    walk_ty(v, out);
                }
                TypeExpr::Generic(_, args) => {
                    for a in args {
                        walk_ty(a, out);
                    }
                }
                TypeExpr::Tuple(elems) => {
                    for e in elems {
                        walk_ty(e, out);
                    }
                }
                TypeExpr::Named(_) => {}
                TypeExpr::Fn(params, ret) => {
                    for p in params {
                        walk_ty(p, out);
                    }
                    walk_ty(ret, out);
                }
            }
        }
        fn walk_expr(e: &Expr, out: &mut Vec<Vec<TypeExpr>>) {
            match e {
                Expr::Tuple(elems) => {
                    // Infer from literals when possible
                    let mut tys = Vec::new();
                    let mut ok = true;
                    for el in elems {
                        match el {
                            Expr::Int(_) => tys.push(TypeExpr::Named("int".into())),
                            Expr::String(_) => tys.push(TypeExpr::Named("string".into())),
                            Expr::Bool(_) => tys.push(TypeExpr::Named("bool".into())),
                            Expr::Float(_) => tys.push(TypeExpr::Named("float".into())),
                            _ => {
                                ok = false;
                                break;
                            }
                        }
                    }
                    if ok && tys.len() >= 2 {
                        out.push(tys);
                    }
                    for el in elems {
                        walk_expr(el, out);
                    }
                }
                Expr::Call { callee, args } => {
                    walk_expr(callee, out);
                    for a in args {
                        walk_expr(a, out);
                    }
                }
                Expr::Binary { left, right, .. } => {
                    walk_expr(left, out);
                    walk_expr(right, out);
                }
                Expr::Unary { expr, .. }
                | Expr::Field { base: expr, .. }
                | Expr::Try(expr)
                | Expr::Join(expr)
                | Expr::Kick { expr, .. } => walk_expr(expr, out),
                Expr::Array(xs) => {
                    for x in xs {
                        walk_expr(x, out);
                    }
                }
                Expr::Match { scrutinee, arms } => {
                    walk_expr(scrutinee, out);
                    for a in arms {
                        walk_expr(&a.body, out);
                    }
                }
                Expr::Block(b) => {
                    for s in &b.stmts {
                        walk_stmt(s, out);
                    }
                }
                Expr::Lambda { body, .. } => walk_expr(body, out),
                Expr::Method { receiver, args, .. } => {
                    walk_expr(receiver, out);
                    for a in args {
                        walk_expr(a, out);
                    }
                }
                Expr::Make { ty, len, cap } => {
                    walk_ty(ty, out);
                    if let Some(l) = len {
                        walk_expr(l, out);
                    }
                    if let Some(c) = cap {
                        walk_expr(c, out);
                    }
                }
                Expr::ChanOpen { elem, cap } => {
                    walk_ty(elem, out);
                    walk_expr(cap, out);
                }
                _ => {}
            }
        }
        fn walk_stmt(s: &Stmt, out: &mut Vec<Vec<TypeExpr>>) {
            match s {
                Stmt::Let { ty, init, .. } => {
                    if let Some(t) = ty {
                        walk_ty(t, out);
                    }
                    walk_expr(init, out);
                }
                Stmt::LetMulti { init, .. } => walk_expr(init, out),
                Stmt::Return(Some(e)) | Stmt::Expr(e) | Stmt::Assign { value: e, .. } => {
                    walk_expr(e, out)
                }
                Stmt::If {
                    init,
                    cond,
                    then_block,
                    else_block,
                } => {
                    if let Some(init) = init {
                        walk_stmt(init, out);
                    }
                    walk_expr(cond, out);
                    for st in &then_block.stmts {
                        walk_stmt(st, out);
                    }
                    if let Some(eb) = else_block {
                        for st in &eb.stmts {
                            walk_stmt(st, out);
                        }
                    }
                }
                Stmt::While { cond, body, .. } => {
                    walk_expr(cond, out);
                    for st in &body.stmts {
                        walk_stmt(st, out);
                    }
                }
                Stmt::For { iter, body, .. } => {
                    walk_expr(iter, out);
                    for st in &body.stmts {
                        walk_stmt(st, out);
                    }
                }
                Stmt::Defer { body }
                | Stmt::Crew { body, .. }
                | Stmt::Arena { body, .. }
                | Stmt::Unsafe { body } => {
                    for st in &body.stmts {
                        walk_stmt(st, out);
                    }
                }
                _ => {}
            }
        }
        for item in &program.items {
            if let Item::Fn(f) = item {
                for p in &f.params {
                    walk_ty(&p.ty, &mut tuples);
                }
                if let Some(r) = &f.ret {
                    walk_ty(r, &mut tuples);
                }
                for s in &f.body.stmts {
                    walk_stmt(s, &mut tuples);
                }
            }
        }
        for elems in tuples {
            let c_tys: Vec<String> = elems.iter().map(|e| self.type_expr_c(e)).collect();
            let tag = c_tys
                .iter()
                .map(|t| c_type_mono_tag(t))
                .collect::<Vec<_>>()
                .join("_");
            let cname = format!("MakoTup_{tag}");
            if self.tuple_typedefs.insert(cname.clone()) {
                let mut fields = String::new();
                for (i, t) in c_tys.iter().enumerate() {
                    fields.push_str(&format!("    {t} _{i};\n"));
                }
                let _ = writeln!(
                    self.out,
                    "typedef struct {{\n{fields}}} {cname};\n"
                );
            }
        }
        // Flush any pending (should be empty here)
        for td in self.pending_tuple_typedefs.drain(..) {
            self.out.push_str(&td);
        }
    }
}

fn fold_const_c_env(
    expr: &Expr,
    consts: &HashMap<String, i64>,
    const_fns: &HashMap<String, FnDef>,
) -> Option<i64> {
    match expr {
        Expr::Int(n) => Some(*n),
        Expr::Ident(n) => consts.get(n).copied(),
        Expr::Binary { op, left, right } => {
            let l = fold_const_c_env(left, consts, const_fns)?;
            let r = fold_const_c_env(right, consts, const_fns)?;
            match op {
                BinOp::Add => Some(l.wrapping_add(r)),
                BinOp::Sub => Some(l.wrapping_sub(r)),
                BinOp::Mul => Some(l.wrapping_mul(r)),
                BinOp::Div if r != 0 => Some(l / r),
                BinOp::Mod if r != 0 => Some(l % r),
                BinOp::BitAnd => Some(l & r),
                BinOp::BitOr => Some(l | r),
                BinOp::BitXor => Some(l ^ r),
                BinOp::BitClear => Some(l & !r),
                BinOp::Shl => Some(l.wrapping_shl((r as u32) & 63)),
                BinOp::Shr => Some(l.wrapping_shr((r as u32) & 63)),
                _ => None,
            }
        }
        Expr::Unary { op, expr } => {
            let v = fold_const_c_env(expr, consts, const_fns)?;
            match op {
                UnaryOp::Neg => Some(v.wrapping_neg()),
                UnaryOp::BitNot => Some(!v),
                UnaryOp::Not => None,
            }
        }
        Expr::Call { callee, args } => {
            let Expr::Ident(fname) = callee.as_ref() else {
                return None;
            };
            let f = const_fns.get(fname)?;
            if f.params.len() != args.len() {
                return None;
            }
            let mut env = consts.clone();
            for (p, a) in f.params.iter().zip(args.iter()) {
                let v = fold_const_c_env(a, consts, const_fns)?;
                env.insert(p.name.clone(), v);
            }
            // Evaluate body
            let mut locals = env;
            for (i, stmt) in f.body.stmts.iter().enumerate() {
                match stmt {
                    Stmt::Return(Some(e)) => return fold_const_c_env(e, &locals, const_fns),
                    Stmt::Let { name, init, .. } => {
                        let v = fold_const_c_env(init, &locals, const_fns)?;
                        locals.insert(name.clone(), v);
                    }
                    Stmt::Expr(e) if i + 1 == f.body.stmts.len() => {
                        return fold_const_c_env(e, &locals, const_fns);
                    }
                    _ => return None,
                }
            }
            None
        }
        _ => None,
    }
}
