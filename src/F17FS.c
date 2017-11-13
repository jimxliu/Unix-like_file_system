#include "dyn_array.h"
#include "bitmap.h"
#include "block_store.h"
#include "F17FS.h"
#include "string.h"
#include "libgen.h"

#define BLOCK_STORE_NUM_BLOCKS 65536   // 2^16 blocks.
#define BLOCK_STORE_AVAIL_BLOCKS 65520 // Last 2^16/2^3/2^9 = 16 blocks consumed by the FBM
#define BLOCK_SIZE_BYTES 512           // 2^9 BYTES per block


// each inode represents a regular file or a directory file
struct inode {
	uint8_t vacantFile;			// this parameter is only for directory, denotes which place in the array is empty and can hold a new file
	char owner[18];

	char fileType;				// 'r' denotes regular file, 'd' denotes directory file
	
	size_t inodeNumber;			// for F17FS, the range should be 0-255
	size_t fileSize; 			// the unit is in byte	
	size_t linkCount;
	
	// to realize the 16-bit addressing, pointers are acutally block numbers, rather than 'real' pointers.
	uint16_t directPointer[6];
	uint16_t indirectPointer[1];
	uint16_t doubleIndirectPointer;
		
};


struct fileDescriptor {
	uint8_t inodeNum;	// the inode # of the fd
	uint8_t usage; 		// only the lower 3 digits will be used. 1 for direct, 2 for indirect, 4 for dbindirect
	// locate_block and locate_offset together lcoate the exact byte
	uint16_t locate_order;		// block_id this term is relative value, rather than a absolute value
	uint16_t locate_offset; // offset from the first byte (0 byte) in the data block
};


struct directoryFile {
	char filename[64];
	uint8_t inodeNumber;
};

struct directoryBlock {
	directoryFile_t dentries[7];
	char padding[57];
};

struct F17FS {
	block_store_t * BlockStore_whole;
	block_store_t * BlockStore_inode;
	block_store_t * BlockStore_fd;
};

// initialize directoryFile to 0 or "";
directoryFile_t init_dirFile(void){
	directoryFile_t df;
	memset(df.filename,'\0',64);
	df.inodeNumber = 0x00;
	return df;
}

// initialize directoryBlock 
directoryBlock_t init_dirBlock(void){
	directoryBlock_t db;
	int i=0;
	for(; i<7; i++){
		db.dentries[i] = init_dirFile();
	}
	memset(db.padding,'\0',57);
	return db;
}

/// Formats (and mounts) an F17FS file for use
/// \param fname The file to format
/// \return Mounted F17FS object, NULL on error
///
F17FS_t *fs_format(const char *path)
{
	if(path != NULL && strlen(path) != 0)
	{
		F17FS_t * ptr_F17FS = malloc(sizeof(F17FS_t));	// get started
		ptr_F17FS->BlockStore_whole = block_store_create(path);				// pointer to start of a large chunck of memory
		
		// reserve the 1st block for bitmaps (this block is cut half and half, for inode bitmap and fd bitmap)
		size_t bitmap_ID = block_store_allocate(ptr_F17FS->BlockStore_whole);
		//printf("bitmap_ID = %zu\n", bitmap_ID);
		// 2nd - 33th block for inodes, 32 blocks in total
		size_t inode_start_block = block_store_allocate(ptr_F17FS->BlockStore_whole);
		//printf("inode_start_block = %zu\n", inode_start_block);		
		for(int i = 0; i < 31; i++)
		{
			block_store_allocate(ptr_F17FS->BlockStore_whole);
		}
		
		// 34th block for root directory
		size_t root_data_ID = block_store_allocate(ptr_F17FS->BlockStore_whole);
		//printf("root_data_ID = %zu\n\n", root_data_ID);				
		// install inode block store inside the whole block store
		ptr_F17FS->BlockStore_inode = block_store_inode_create(block_store_Data_location(ptr_F17FS->BlockStore_whole) + bitmap_ID * BLOCK_SIZE_BYTES, block_store_Data_location(ptr_F17FS->BlockStore_whole) + inode_start_block * BLOCK_SIZE_BYTES);

		// the first inode is reserved for root dir
		block_store_sub_allocate(ptr_F17FS->BlockStore_inode);
		
		// update the root inode info.
		uint8_t root_inode_ID = 0;	// root inode is the first one in the inode table
		inode_t * root_inode = (inode_t *) calloc(1, sizeof(inode_t));
		root_inode->vacantFile = 0x00;
		root_inode->fileType = 'd';
		root_inode->fileSize = BLOCK_SIZE_BYTES;								
		root_inode->inodeNumber = root_inode_ID;
		root_inode->linkCount = 1;
		root_inode->directPointer[0] = root_data_ID;
		block_store_inode_write(ptr_F17FS->BlockStore_inode, root_inode_ID, root_inode);
		directoryBlock_t rootDataBlock = init_dirBlock();		
		block_store_write(ptr_F17FS->BlockStore_whole, root_data_ID,&rootDataBlock );
		free(root_inode);
		
		// now allocate space for the file descriptors
		ptr_F17FS->BlockStore_fd = block_store_fd_create();

		return ptr_F17FS;
	}	
	return NULL;	
}




///
/// Mounts an F17FS object and prepares it for use
/// \param fname The file to mount

/// \return Mounted F17FS object, NULL on error

///
F17FS_t *fs_mount(const char *path)
{
	if(path != NULL && strlen(path) != 0)
	{
		F17FS_t * ptr_F17FS = (F17FS_t *)calloc(1, sizeof(F17FS_t));	// get started
		ptr_F17FS->BlockStore_whole = block_store_open(path);	// get the chunck of data	
		
		// the bitmap block should be the 1st one
		size_t bitmap_ID = 0;

		// the inode blocks start with the 2nd block, and goes around until the 33th block, 32 in total
		size_t inode_start_block = 1;
		
		// attach the bitmaps to their designated place
		ptr_F17FS->BlockStore_inode = block_store_inode_create(block_store_Data_location(ptr_F17FS->BlockStore_whole) + bitmap_ID * BLOCK_SIZE_BYTES, block_store_Data_location(ptr_F17FS->BlockStore_whole) + inode_start_block * BLOCK_SIZE_BYTES);
		
		// since file descriptors are alloacted outside of the whole blocks, we only can reallocate space for it.
		ptr_F17FS->BlockStore_fd = block_store_fd_create();
		
		return ptr_F17FS;
	}
	
	return NULL;		
}




///
/// Unmounts the given object and frees all related resources
/// \param fs The F17FS object to unmount
/// \return 0 on success, < 0 on failure
///
int fs_unmount(F17FS_t *fs)
{
	if(fs != NULL)
	{	
		block_store_inode_destroy(fs->BlockStore_inode);
		
		block_store_destroy(fs->BlockStore_whole);
		block_store_fd_destroy(fs->BlockStore_fd);
		
		free(fs);
		return 0;
	}
	return -1;
}

///
/// Creates a new file at the specified location
///   Directories along the path that do not exist are not created
/// \param fs The F17FS containing the file
/// \param path Absolute path to file to create
/// \param type Type of file to create (regular/directory)
/// \return 0 on success, < 0 on failure
//
int fs_create(F17FS_t *fs, const char *path, file_t type){
	if(fs == NULL || path == NULL || (type != FS_REGULAR && type != FS_DIRECTORY) || strlen(path) <= 1){
		return -1;
	}
	// check if inode table is full
	if(block_store_get_used_blocks(fs->BlockStore_inode) >= 256){
		return -14;
	}
	// valid path must start with '/'
	char firstChar = *path;
	//printf("path: %s\nfirstChar: %c\n",path,firstChar);
	if(firstChar != '/'){return -2;}
	// path cannot end with "/"
	char lastChar = path[strlen(path)-1];
	//printf("path: %s\nlastChar: %c\n",path,lastChar);
	if(lastChar == '/'){return -13;}
	char dirc[strlen(path)+1]; // p is the non const copy of path
	char basec[strlen(path)+1];
	strcpy(dirc,path);
	strcpy(basec,path);
	char *dirPath = dirname(dirc);
	char *baseFileName = basename(basec);
	//printf("path: %s\ndirPath: %s\nbaseFileName: %s\n",path,dirPath,baseFileName);
	if(strlen(baseFileName)>=64){return -3;}
	char *fn = strtok(dirPath,"/"); // every directory's name along the path

	// set fileType
	char fileType = 'r';
	if(type == FS_DIRECTORY){fileType = 'd';}

	// search and check if the directory name "fn" along the path are valid
	size_t iNum = 0; // inode number of the searched directory inode
	inode_t dirInode; // inode of the searched directory
	directoryBlock_t dirBlock; // file block of the searched directory
	while(fn != NULL){
		// find the inode
		if(0 == block_store_inode_read(fs->BlockStore_inode,iNum,&dirInode)){
			return -4;
		}
		// the inode must be of directory
		if(dirInode.fileType != 'd'){
			return -5;
		}
		// read the directory file block 	
		if(0 == block_store_read(fs->BlockStore_whole,dirInode.directPointer[0],&dirBlock)){
			return -6;
		}
		// search in the entries of the directory to see if the next directory name is found
		// use bitmap to jump over uninitialzied(unused) entries
		bitmap_t * dirBitmap = bitmap_overlay(8,&(dirInode.vacantFile));
		size_t j=0, found = 0;
		for(; j<7; j++){
			if(!bitmap_test(dirBitmap,j)){continue;}		
			if(strncmp(dirBlock.dentries[j].filename,fn,strlen(fn)) == 0 /* && (0 < dirBlock.dentries[j].inodeNumber)*/){
				inode_t nextInode; // inode whoes filename is fn
				// check if it is found and is dir  
				if((0 != block_store_inode_read(fs->BlockStore_inode,dirBlock.dentries[j].inodeNumber,&nextInode)) && (nextInode.fileType == 'd')){
					iNum = nextInode.inodeNumber;
					found = 1;
				}
			}
		}
		bitmap_destroy(dirBitmap);
		// if not found, exit on error
		if(found == 0){
			return -7;		
		}
		fn = strtok(NULL,"/");
	}
	// file aready exists?? use iNum as the inode number of the parent dir to search if this directory already contains the file/dir to be created
	inode_t parentInode; // inode for the parent directory of the destinated file/dir
	directoryBlock_t parentDir; // directory file block of the parent directory
	if((0==block_store_inode_read(fs->BlockStore_inode,iNum,&parentInode)) || (0==block_store_read(fs->BlockStore_whole, parentInode.directPointer[0],&parentDir))){
		return -8;
	}
	// use bitmap to jump over uninitialized(unused) entries
	bitmap_t *parentBitmap = bitmap_overlay(8, &(parentInode.vacantFile)); 	
	int k=0;
	for(; k<7; k++){
		if(!bitmap_test(parentBitmap,k)){continue;}
		if(0 == strncmp(parentDir.dentries[k].filename,baseFileName,strlen(baseFileName)) /* && 0 < parentDir.dentries[k].inodeNumber*/){
			// do not worry about same name but different file type.
			// files can't have same name, regardless of file type.
			//inode_t tempInode;
			//if((0 != block_store_inode_read(fs->BlockStore_inode,parentDir.dentries[k].inodeNumber,&tempInode)) && (tempInode.fileType == fileType)){
		        // printf("path: %s\nfilename already exists: %s\n",path,parentDir.dentries[k].filename);	
			bitmap_destroy(parentBitmap);
			return -9;
			//}
		}
	}
	bitmap_destroy(parentBitmap);	

	// check if the dir block is full of entries using bitmap
	// only 7 entries are allowed in a directory, 0 - 6 bit, not including 7
	size_t available = 7;
	bitmap_t *bmp = bitmap_overlay(8,&(parentInode.vacantFile));
	available = bitmap_ffz(bmp);
	//printf("path: %s\ndirPath: %s\navailable: %lu\n\n",path,dirPath,available);
	if(available == SIZE_MAX || available == 7){
		bitmap_destroy(bmp);
		return -10;
	} 
  	
	// allocate a new inode for the new file and get its inode number
	size_t newInodeID = block_store_sub_allocate(fs->BlockStore_inode);
	// create a new inode for the file
	inode_t newInode;
	newInode.fileType = fileType;
	newInode.directPointer[0] = block_store_allocate(fs->BlockStore_whole);
	if(fileType == 'd'){
		newInode.vacantFile = 0x00;
		directoryBlock_t newDirBlock;
		block_store_write(fs->BlockStore_whole, newInode.directPointer[0],&newDirBlock);
		newInode.fileSize = BLOCK_SIZE_BYTES;	
	} else {
		newInode.fileSize = 0;
	}
	newInode.inodeNumber = newInodeID;
	newInode.linkCount = 1;
	// write the created inode to the inode table
	block_store_inode_write(fs->BlockStore_inode,newInodeID,&newInode);
	
	// add a new entry of filename and inode number to the parentDir 
	// write it to the block store
	bitmap_set(bmp, available);
	if(64 != block_store_inode_write(fs->BlockStore_inode, parentInode.inodeNumber, &parentInode)){
		bitmap_destroy(bmp);
		return -11;
	}
	directoryFile_t df;
	strcpy(df.filename, baseFileName);
	df.inodeNumber = newInodeID; 
	//printf("baseFileName: %s inodeNumber: %d\n", baseFileName, newInodeID);
	parentDir.dentries[available] = df;
	if(0==block_store_write(fs->BlockStore_whole,parentInode.directPointer[0],&parentDir)){
		bitmap_destroy(bmp);
		return -12;
	}
	
	bitmap_destroy(bmp);	
	// printf("dirPath: %s \n baseName: %s\n",dirPath, baseFileName);
	return 0;   
}

// search if the absolute path leading to the directory exists
// \param fs F17FS File system containing the file
// \param dirPath Absolute path of the directory
// return the inode number of the directory,  SIZE_MAX on error
size_t searchPath(F17FS_t *fs, char* dirPath){
	char *fn = strtok(dirPath,"/");
	// search and check if the directory name "fn" along the path are valid
	size_t iNum = 0; // inode number of the searched directory inode
	inode_t dirInode; // inode of the searched directory
	directoryBlock_t dirBlock; // file block of the searched directory
	while(fn != NULL){
		// find the inode
		if(0 == block_store_inode_read(fs->BlockStore_inode,iNum,&dirInode)){
			return SIZE_MAX;
		}
		// the inode must be of directory
		if(dirInode.fileType != 'd'){
			return SIZE_MAX;
		}
		// read the directory file block 	
		if(0 == block_store_read(fs->BlockStore_whole,dirInode.directPointer[0],&dirBlock)){
			return SIZE_MAX;
		}
		// search in the entries of the directory to see if the next directory name is found
		// use bitmap to jump over uninitialzied(unused) entries
		bitmap_t * dirBitmap = bitmap_overlay(8,&(dirInode.vacantFile));
		size_t j=0, found = 0;
		for(; j<7; j++){
			if(!bitmap_test(dirBitmap,j)){continue;}		
			if(strncmp(dirBlock.dentries[j].filename,fn,strlen(fn)) == 0 /* && (0 < dirBlock.dentries[j].inodeNumber)*/){
				inode_t nextInode; // inode whoes filename is fn
				// check if it is found and is dir  
				if((0 != block_store_inode_read(fs->BlockStore_inode,dirBlock.dentries[j].inodeNumber,&nextInode)) && (nextInode.fileType == 'd')){
					iNum = nextInode.inodeNumber;
					found = 1;
				}
			}
		}
		bitmap_destroy(dirBitmap);
		// if not found, exit on error
		if(found == 0){
			return SIZE_MAX;		
		}
		fn = strtok(NULL,"/");
	}
	return iNum;
} 

// check if the file already exists under the designated directory, if so, get the file's inode number
// \param fs F17FS containing the file
// \param dirInodeID The inode number of the directory
// \param filename Name of the file to look for
// return the file's inode number if the file is already created (exists), or 0 otherwise
size_t getFileInodeID(F17FS_t *fs, size_t dirInodeID, char *filename){
	// file aready exists?? use iNum as the inode number of the parent dir to search if this directory already contains the file/dir to be created
	inode_t parentInode; // inode for the parent directory of the destinated file/dir
	directoryBlock_t parentDir; // directory file block of the parent directory
	if((0==block_store_inode_read(fs->BlockStore_inode,dirInodeID,&parentInode)) || (0==block_store_read(fs->BlockStore_whole, parentInode.directPointer[0],&parentDir))){
		return 0;
	}
	// use bitmap to jump over uninitialized(unused) entries
	bitmap_t *parentBitmap = bitmap_overlay(8, &(parentInode.vacantFile)); 	
	int k=0;
	for(; k<7; k++){
		if(!bitmap_test(parentBitmap,k)){continue;}
		if(0 == strncmp(parentDir.dentries[k].filename,filename,strlen(filename)) /* && 0 < parentDir.dentries[k].inodeNumber*/){
			// do not worry about same name but different file type.
			// files can't have same name, regardless of file type.
			//inode_t tempInode;
			//if((0 != block_store_inode_read(fs->BlockStore_inode,parentDir.dentries[k].inodeNumber,&tempInode)) && (tempInode.fileType == fileType)){
		        // printf("path: %s\nfilename already exists: %s\n",path,parentDir.dentries[k].filename);	
			bitmap_destroy(parentBitmap);
			return parentDir.dentries[k].inodeNumber;
			//}
		}
	}
	bitmap_destroy(parentBitmap);
	return 0;
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
	if(fs == NULL || path == NULL || strlen(path) <= 1){
		return -1;
	}
	// valid path must start with '/'
	char firstChar = *path;
	//printf("path: %s\nfirstChar: %c\n",path,firstChar);
	if(firstChar != '/'){return -2;}
	// path cannot end with "/"
	char lastChar = path[strlen(path)-1];
	//printf("path: %s\nlastChar: %c\n",path,lastChar);
	if(lastChar == '/'){return -3;}

	char dirc[strlen(path)+1]; // p is the non const copy of path
	char basec[strlen(path)+1];
	strcpy(dirc,path);
	strcpy(basec,path);
	char *dirPath = dirname(dirc);
	char *baseFileName = basename(basec);
	//printf("path: %s\ndirPath: %s\nbaseFileName: %s\n",path,dirPath,baseFileName);
	if(strlen(baseFileName)>=64){return -4;}

	size_t dirInodeID = searchPath(fs,dirPath);
	if(dirInodeID == SIZE_MAX){return -5;} // No such path for the dir containing the requested file
	size_t fileInodeID = getFileInodeID(fs,dirInodeID,baseFileName);
	if(fileInodeID == 0){return -6;} // No such file is found
	inode_t fileInode;
	if(0 == block_store_inode_read(fs->BlockStore_inode,fileInodeID,&fileInode)){return -7;} // get the inode object of the file
	if('d'==fileInode.fileType){return -8;} // file can't be directory
	size_t fd = block_store_sub_allocate(fs->BlockStore_fd); // file descriptor ID
	fileDescriptor_t fd_t;
	fd_t.inodeNum = fileInodeID;	
	fd_t.usage = 1;
	fd_t.locate_order = fileInode.directPointer[0];
	fd_t.locate_offset = 0;
	if(0 == block_store_fd_write(fs->BlockStore_fd,fd,&fd_t)){return -9;}			
	return fd;
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
	if(!block_store_sub_test(fs->BlockStore_fd,fd)){return -2;}
	block_store_sub_release(fs->BlockStore_fd,fd); 
	return 0;
}






