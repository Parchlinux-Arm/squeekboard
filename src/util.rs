/*! Assorted helpers */
use std::collections::HashMap;
use std::rc::Rc;

use ::float_ord::FloatOrd;

use std::borrow::Borrow;
use std::hash::{ Hash, Hasher };
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
    
    // The lifetime on input limits the existence of the result
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
    
    /// Marker trait for values that can be transferred to/received from C.
    /// They must be either *const or *mut or repr(transparent).
    pub trait COpaquePtr {}

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
        pub fn new(value: T) -> Wrapped<T> {
            Wrapped::wrap(Rc::new(RefCell::new(value)))
        }
        pub fn wrap(state: Rc<RefCell<T>>) -> Wrapped<T> {
            Wrapped(Rc::into_raw(state))
        }
        /// Extracts the reference to the data.
        /// It may cause problems if attempted in more than one place
        // FIXME: check for null
        pub unsafe fn unwrap(self) -> Rc<RefCell<T>> {
            Rc::from_raw(self.0)
        }
        
        /// Creates a new Rc reference to the same data.
        /// Use for accessing the underlying data as a reference.
        pub fn clone_ref(&self) -> Rc<RefCell<T>> {
            // A bit dangerous: the Rc may be in use elsewhere
            let used_rc = unsafe { Rc::from_raw(self.0) };
            let rc = used_rc.clone();
            Rc::into_raw(used_rc); // prevent dropping the original reference
            rc
        }

        pub fn is_null(&self) -> bool {
            self.0.is_null()
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
            let r = RefCell::borrow(&rc);
            r.to_owned()
        }
    }

    impl<T> COpaquePtr for Wrapped<T> {}
}

/// Clones the underlying data structure, like ToOwned.
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

pub fn find_max_double<T, I, F>(iterator: I, get: F)
    -> f64
    where I: Iterator<Item=T>,
        F: Fn(&T) -> f64
{
    iterator.map(|value| FloatOrd(get(&value)))
        .max().unwrap_or(FloatOrd(0f64))
        .0
}

/// Compares pointers but not internal values of Rc
pub struct Pointer<T>(pub Rc<T>);

impl<T> Pointer<T> {
    pub fn new(value: T) -> Self {
        Pointer(Rc::new(value))
    }
}

impl<T> Hash for Pointer<T> {
    fn hash<H: Hasher>(&self, state: &mut H) {
        (&*self.0 as *const T).hash(state);
    }
}

impl<T> PartialEq for Pointer<T> {
    fn eq(&self, other: &Pointer<T>) -> bool {
        Rc::ptr_eq(&self.0, &other.0)
    }
}

impl<T> Eq for Pointer<T> {}

impl<T> Clone for Pointer<T> {
    fn clone(&self) -> Self {
        Pointer(self.0.clone())
    }
}

impl<T> Borrow<Rc<T>> for Pointer<T> {
    fn borrow(&self) -> &Rc<T> {
        &self.0
    }
}

pub trait WarningHandler {
    /// Handle a warning
    fn handle(&mut self, warning: &str);
}

/// Removes the first matcing item
pub fn vec_remove<T, F: FnMut(&T) -> bool>(v: &mut Vec<T>, pred: F) -> Option<T> {
    let idx = v.iter().position(pred);
    idx.map(|idx| v.remove(idx))
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::collections::HashSet;

    #[test]
    fn check_set() {
        let mut s = HashSet::new();
        let first = Rc::new(1u32);
        s.insert(Pointer(first.clone()));
        assert_eq!(s.insert(Pointer(Rc::new(2u32))), true);
        assert_eq!(s.remove(&Pointer(first)), true);
    }
}
