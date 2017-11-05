#include <block_store.h>
#include <F17FS.h>
#include <bitmap.h>
#include <string.h>

typedef struct superblock {
	int root_inode; // inode number of root dir
	int ibm_block_id; 
	int inode_start; // first block for inodes
	int inode_blocks; // number of inode blocks
	int fbm_start; //first block of bitmap
	int fbm_blocks; // number of bloks used to store the bitmap
	int alloc_start; // first block managed by the allocator
	int num_blocks; // number of blocks for allocation	 
} superblock_t;

typedef struct inode {
	file_t type;  // 4 bytes
	uint32_t size;
	uint32_t padding1[8]; // biggest chunk, size == 32 bytes
	uint32_t padding2[2];
	uint16_t direct_blocks[6];
	uint16_t indirect_block;
	uint16_t double_block;
} inode_t;

typedef struct dentry { // what is the correct way of implementing direcotry blocks?
	char name[64];
	char i_num;
} dentry_t;

typedef struct dir_block{
	dentry_t dentries[7];
	char length;
	char padding[56];
} dir_block_t;

struct F17FS { // typedef-ed in header file
	block_store_t *bs;
};

// initialize inode for file/directory
inode_t init_inode(F17FS_t *fs,file_t type){
        inode_t i;
        if(type==FS_DIRECTORY){
                i.type = type;
                i.size = 512;
                size_t id;
                id = block_store_allocate(fs->bs);
                if(id != SIZE_MAX){
                        i.direct_blocks[0] = id;
                }
        } else if(type==FS_REGULAR){
                i.type = type;
                i.size = 0;
        }
        return i;
}
//initialize directory file block
//\param current_i inode number for current directory
//\param parent_i inode number for parent directory
dir_block_t init_db(char current_i, char parent_i){
	dir_block_t db;
	strcpy(db.dentries[0].name,".");
	db.dentries[0].i_num = current_i;
	strcpy(db.dentries[1].name,"..");
	db.dentries[1].i_num = parent_i;
	db.length = 2;
	return db;
}

///
/// Formats (and mounts) an F17FS file for use
/// \param fname The file to format
/// \return Mounted F17FS object, NULL on error
///

F17FS_t *fs_format(const char *path){
	if(path != NULL){
		F17FS_t *fs = malloc(sizeof(F17FS_t));
		if(fs !=NULL){
			fs->bs = block_store_create(path);
			if(fs->bs != NULL){
				if(block_store_request(fs->bs,0)){
					superblock_t sb;
					sb.root_inode = 1;
					sb.ibm_block_id = 2;
					sb.inode_start = 3;
					sb.inode_blocks = 32;
					sb.fbm_start = 65221;
					sb.fbm_blocks = 16;
					sb.alloc_start = 35;
					sb.num_blocks = 65186;
					
					if(block_store_write(fs->bs,1,&sb)!=0){
        				// allocate blocks for supoerblock, inode table.
           					int i;
						bool allocated_inode_table = true;
						for(i=2; i<35; i++){
							if(block_store_request(fs->bs,i) == false){
								allocated_inode_table = false;
							}	
						}
						if(allocated_inode_table){
						//  maximum size 32 blocks * 512 bytes 
						//	each inode is 64 bytes	
						//	memcpy 
						// create inode bitmap and root inode
							char i_table[256];
							i_table[0]=1;
							i_table[1]=1;	
							if(block_store_write(fs->bs,2,i_table)){
	
							  	inode_t ilist[8];
								int j;
								bool write_i_table = true;
								for(j=3;j<35;j++){
									if(block_store_write(fs->bs,j,ilist)==0){
										write_i_table = false;
									}									
								}
								ilist[0] = init_inode(fs,FS_DIRECTORY);
								if(block_store_write(fs->bs,3,ilist)==0){
									write_i_table = false;
								}
								if(write_i_table){
								// allocate a data block for root directory entries;
									uint16_t root_block_id = ilist[0].direct_blocks[0];
									dir_block_t root_db;
									root_db = init_db(1,1);
									if(block_store_write(fs->bs,root_block_id,&root_db)){
										return fs;
									}									 	
								}		
							}		
						}	  
					}  
				}
				
			}
			block_store_destroy(fs->bs);
			free(fs);
			return NULL;	
		}
		free(fs);
	}
	return NULL;
}


///
/// Mounts an F17FS object and prepares it for use
/// \param fname The file to mount

/// \return Mounted F17FS object, NULL on error

///
F17FS_t *fs_mount(const char *path){
   if(path == NULL){return NULL;} 
   F17FS_t *fs = malloc(sizeof(F17FS_t));
	if(fs){
		fs->bs=block_store_open(path);
		if(fs->bs){
			return fs;
		}
		free(fs);
	}
	return NULL;   
}

///
/// Unmounts the given object and frees all related resources
/// \param fs The F17FS object to unmount
/// \return 0 on success, < 0 on failure
///
int fs_unmount(F17FS_t *fs){
   if(fs == NULL){
		return -1;
	} 
	block_store_destroy(fs->bs);
	free(fs);
	return 0;
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
   if(fs == NULL || path == NULL || type == FS_REGULAR ){
		return -1;
	} 
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
	if(fs == NULL || path == NULL){
		return -1;
 	}
	return -1;
}

///
/// Closes the given file descriptor
/// \param fs The F17FS containing the file
/// \param fd The file to close
/// \return 0 on success, < 0 on failure
///
int fs_close(F17FS_t *fs, int fd){
  	if(fs == NULL || fd < 0){
		return -1;
	} 
	return -1;
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
   if(fs==NULL||fd<0||offset==0||whence<0){
		return -1;
	} 
	return -1;
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
   if(fs==NULL||fd<0||dst==NULL||nbyte==0){
		return -1;
	} 
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
   if(fs==NULL||fd<0||src==NULL||nbyte==0){
		return -1;
	} 
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
   if(fs==NULL||path==NULL){return -1;} 
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
   if(fs==NULL||path==NULL){return NULL;} 
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
   if(fs==NULL||src==NULL||dst==NULL){
		return -1;
	} 
	return -1;
}

