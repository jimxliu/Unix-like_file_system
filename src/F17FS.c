#include <block_store.h>
#include <F17FS.h>


///
/// Formats (and mounts) an F17FS file for use
/// \param fname The file to format
/// \return Mounted F17FS object, NULL on error
///

F17FS_t fs_format(const char *path){
    return NULL;
}

///
/// Mounts an F17FS object and prepares it for use
/// \param fname The file to mount

/// \return Mounted F17FS object, NULL on error

///
F17FS_t *fs_mount(const char *path){
    return NULL;   
}

///
/// Unmounts the given object and frees all related resources
/// \param fs The F17FS object to unmount
/// \return 0 on success, < 0 on failure
///
int fs_unmount(F17FS_t *fs){
    return -1;
}


///
/// Creates a new file at the specified location
///   Directories along the path that do not exist are not created
/// \param fs The F17FS containing the file
/// \param path Absolute path to file to create
/// \param type Type of file to create (regular/directory)
/// \return 0 on success, < 0 on failure
///
int fs_create(F17FS_t *fs, const char *path, file_t type){
    return -1;   
}

///
/// Opens the specified file for use
///   R/W position is set to the beginning of the file (BOF)
///   Directories cannot be opened
/// \param fs The F17FS containing the file
/// \param path path to the requested file
/// \return file descriptor to the requested file, < 0 on error
///
int fs_open(F17FS_t *fs, const char *path){
    return -1;
}

///
/// Closes the given file descriptor
/// \param fs The F17FS containing the file
/// \param fd The file to close
/// \return 0 on success, < 0 on failure
///
int fs_close(F17FS_t *fs, int fd){
    return -1
}

///
/// Moves the R/W position of the given descriptor to the given location
///   Files cannot be seeked past EOF or before BOF (beginning of file)
///   Seeking past EOF will seek to EOF, seeking before BOF will seek to BOF
/// \param fs The F17FS containing the file
/// \param fd The descriptor to seek
/// \param offset Desired offset relative to whence
/// \param whence Position from which offset is applied
/// \return offset from BOF, < 0 on error
///
off_t fs_seek(F17FS_t *fs, int fd, off_t offset, seek_t whence){
    return -1
}

///
/// Reads data from the file linked to the given descriptor
///   Reading past EOF returns data up to EOF
///   R/W position in incremented by the number of bytes read
/// \param fs The F17FS containing the file
/// \param fd The file to read from
/// \param dst The buffer to write to
/// \param nbyte The number of bytes to read
/// \return number of bytes read (< nbyte IFF read passes EOF), < 0 on error
///
ssize_t fs_read(F17FS_t *fs, int fd, void *dst, size_t nbyte){
    return -1;
}

///
/// Writes data from given buffer to the file linked to the descriptor
///   Writing past EOF extends the file
///   Writing inside a file overwrites existing data
///   R/W position in incremented by the number of bytes written
/// \param fs The F17FS containing the file
/// \param fd The file to write to
/// \param dst The buffer to read from
/// \param nbyte The number of bytes to write
/// \return number of bytes written (< nbyte IFF out of space), < 0 on error
///
ssize_t fs_write(F17FS_t *fs, int fd, const void *src, size_t nbyte){
    return -1;
}

///
/// Deletes the specified file and closes all open descriptors to the file
///   Directories can only be removed when empty
/// \param fs The F17FS containing the file
/// \param path Absolute path to file to remove
/// \return 0 on success, < 0 on error
///
int fs_remove(F17FS_t *fs, const char *path){
    return -1;
}

///
/// Populates a dyn_array with information about the files in a directory
///   Array contains up to 15 file_record_t structures
/// \param fs The F17FS containing the file
/// \param path Absolute path to the directory to inspect
/// \return dyn_array of file records, NULL on error
///
dyn_array_t *fs_get_dir(F17FS_t *fs, const char *path){
    return NULL;
}

/// Moves the file from one location to the other
///   Moving files does not affect open descriptors
/// \param fs The F17FS containing the file
/// \param src Absolute path of the file to move
/// \param dst Absolute path to move the file to
/// \return 0 on success, < 0 on error
///
int fs_move(F17FS_t *fs, const char *src, const char *dst){
    return -1;
}

