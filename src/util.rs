/*! Assorted helpers */
use std::collections::HashMap;

use std::iter::FromIterator;

pub mod c {
    use super::*;
    
    use std::cell::RefCell;
    use std::ffi::{ CStr, CString };
    use std::os::raw::c_char;
    use std::rc::Rc;
    use std::str::Utf8Error;

    // traits
    
    use std::borrow::ToOwned;
    
    pub fn as_str(s: &*const c_char) -> Result<Option<&str>, Utf8Error> {
        if s.is_null() {
            Ok(None)
        } else {
            unsafe {CStr::from_ptr(*s)}
                .to_str()
                .map(Some)
        }
    }

    pub fn as_cstr(s: &*const c_char) -> Option<&CStr> {
        if s.is_null() {
            None
        } else {
            Some(unsafe {CStr::from_ptr(*s)})
        }
    }

    pub fn into_cstring(s: *const c_char) -> Result<Option<CString>, std::ffi::NulError> {
        if s.is_null() {
            Ok(None)
        } else {
            CString::new(
                unsafe {CStr::from_ptr(s)}.to_bytes()
            ).map(Some)
        }
    }
    
    #[cfg(test)]
    mod tests {
        use super::*;
        use std::ptr;
        
        #[test]
        fn test_null_cstring() {
            assert_eq!(into_cstring(ptr::null()), Ok(None))
        }
        
        #[test]
        fn test_null_str() {
            assert_eq!(as_str(&ptr::null()), Ok(None))
        }
    }

    /// Wraps structures to pass them safely to/from C
    /// Since C doesn't respect borrowing rules,
    /// RefCell will enforce them dynamically (only 1 writer/many readers)
    /// Rc is implied and will ensure timely dropping
    #[repr(transparent)]
    pub struct Wrapped<T>(*const RefCell<T>);

    // It would be nice to implement `Borrow`
    // directly on the raw pointer to avoid one conversion call,
    // but the `borrow()` call needs to extract a `Rc`,
    // and at the same time return a reference to it (`std::cell::Ref`)
    // to take advantage of `Rc<RefCell>::borrow()` runtime checks.
    // Unfortunately, that needs a `Ref` struct with self-referential fields,
    // which is a bit too complex for now.

    impl<T> Wrapped<T> {
        pub fn wrap(state: Rc<RefCell<T>>) -> Wrapped<T> {
            Wrapped(Rc::into_raw(state))
        }
        /// Extracts the reference to the data.
        /// It may cause problems if attempted in more than one place
        pub unsafe fn unwrap(self) -> Rc<RefCell<T>> {
            Rc::from_raw(self.0)
        }
        
        /// Creates a new Rc reference to the same data
        pub fn clone_ref(&self) -> Rc<RefCell<T>> {
            // A bit dangerous: the Rc may be in use elsewhere
            let used_rc = unsafe { Rc::from_raw(self.0) };
            let rc = used_rc.clone();
            Rc::into_raw(used_rc); // prevent dropping the original reference
            rc
        }
    }
    
    impl<T> Clone for Wrapped<T> {
        fn clone(&self) -> Wrapped<T> {
            Wrapped::wrap(self.clone_ref())
        }
    }
    
    /// ToOwned won't work here
    /// because it's really difficult to implement Borrow on Wrapped<T>
    /// with the Rc<RefCell<>> chain on the way to the data
    impl<T: Clone> CloneOwned for Wrapped<T> {
        type Owned = T;

        fn clone_owned(&self) -> T {
            let rc = self.clone_ref();
            let r = rc.borrow();
            r.to_owned()
        }
    }
}

pub trait CloneOwned {
    type Owned;
    fn clone_owned(&self) -> Self::Owned;
}

pub fn hash_map_map<K, V, F, K1, V1>(map: HashMap<K, V>, mut f: F)
    -> HashMap<K1, V1>
    where F: FnMut(K, V) -> (K1, V1),
        K1: std::cmp::Eq + std::hash::Hash
{
    HashMap::from_iter(
        map.into_iter().map(|(key, value)| f(key, value))
    )
}
