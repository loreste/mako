use std::ffi::{c_char, CString, OsStr};
use std::os::unix::ffi::OsStrExt;

unsafe extern "C" {
    fn mako_lld_macho_main(argc: i32, argv: *const *const c_char) -> i32;
}

pub fn link_macho(arguments: &[&OsStr]) -> Result<(), String> {
    let arguments = arguments
        .iter()
        .map(|argument| {
            CString::new(argument.as_bytes())
                .map_err(|_| "bundled lld argument contains a NUL byte".to_owned())
        })
        .collect::<Result<Vec<_>, _>>()?;
    let pointers = arguments
        .iter()
        .map(|argument| argument.as_ptr())
        .collect::<Vec<_>>();
    let status = unsafe { mako_lld_macho_main(pointers.len() as i32, pointers.as_ptr()) };
    if status == 0 {
        Ok(())
    } else {
        Err(format!("bundled lld failed with exit status {status}"))
    }
}
