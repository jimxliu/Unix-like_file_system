#include "dyn_array.h"
#include "bitmap.h"
#include "block_store.h"
#include "F17FS.h"
#include "string.h"
#include "libgen.h"
#include "math.h"

#define BLOCK_STORE_NUM_BLOCKS 65536   // 2^16 blocks.
#define BLOCK_STORE_AVAIL_BLOCKS 65520 // Last 2^16/2^3/2^9 = 16 blocks consumed by the FBM
#define BLOCK_SIZE_BYTES 512           // 2^9 BYTES per block
// direct each: 512, total size: 512 * 6 = 3072
// indirect, index block: 512/2 = 256 addresses, total size: 512 * 256 = 131072
// double indirect, first index block: 256 addresses, second index block total: 256*256 = 65536, total: 65536 * 512 = 33554432  
#define DIRECT_TOTAL_BYTES 3072	
#define SINGLE_INDIRECT_TOTAL_BYTES 131072
#define DOUBLE_INDIRECT_TOTAL_BYTES 33554432
#define DIRECT_BLOCKS 6	
#define INDIRECT_BLOCKS 256
#define DOUBLE_INDIRECT_BLOCKS 65536
#define MAX_FILE_SIZE 33688576

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
	uint16_t indirectPointer;
	uint16_t doubleIndirectPointer;
		
};


struct fileDescriptor {
	uint8_t inodeNum;	// the inode # of the fd
	uint8_t usage; 		// only the lower 3 digits will be used. 1 for direct, 2 for indirect, 4 for dbindirect
	// locate_block and locate_offset together lcoate the exact byte
	uint16_t locate_order;		// the n-th block in the direct, indirect, or dbindirect pointer
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
		if(bitmap_test(parentBitmap,k)){
			if(0 == strncmp(parentDir.dentries[k].filename,filename,strlen(filename)) /* && 0 < parentDir.dentries[k].inodeNumber*/){
				// DO NOT worry about same name but different file type.
				// files can't have same name, regardless of file type.
				//inode_t tempInode;
				//if((0 != block_store_inode_read(fs->BlockStore_inode,parentDir.dentries[k].inodeNumber,&tempInode)) && (tempInode.fileType == fileType)){
		        	// printf("path: %s\nfilename already exists: %s\n",path,parentDir.dentries[k].filename);	
				bitmap_destroy(parentBitmap);
				return parentDir.dentries[k].inodeNumber;
				//}
			}
		}
	}
	bitmap_destroy(parentBitmap);
	return 0;
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
		return -2;
	}
	// valid path must start with '/'
	char firstChar = *path;
	//printf("path: %s\nfirstChar: %c\n",path,firstChar);
	if(firstChar != '/'){return -3;}
	// path cannot end with "/"
	char lastChar = path[strlen(path)-1];
	//printf("path: %s\nlastChar: %c\n",path,lastChar);
	if(lastChar == '/'){return -4;}
	char dirc[strlen(path)+1]; // p is the non const copy of path
	char basec[strlen(path)+1];
	strcpy(dirc,path);
	strcpy(basec,path);
	char *dirPath = dirname(dirc);
	char *baseFileName = basename(basec);
	//printf("path: %s\ndirPath: %s\nbaseFileName: %s\n",path,dirPath,baseFileName);
	if(strlen(baseFileName)>=64){return -5;}

	// set fileType
	char fileType = 'r';
	if(type == FS_DIRECTORY){fileType = 'd';}

	// search and check if the directory name "fn" along the path are valid
	size_t iNum; // inode number of the searched directory inode
	iNum = searchPath(fs,dirPath);
	if(iNum == SIZE_MAX){return -6;}

	// file aready exists?? use iNum as the inode number of the parent dir to search if this directory already contains the file/dir to be created
	if(0!=getFileInodeID(fs,iNum,baseFileName)){ return -7;}		
	
	inode_t parentInode; // get the inode for the parent directory of the destinated file/dir
	directoryBlock_t parentDir; // get the directory file block of the parent directory
	if((0==block_store_inode_read(fs->BlockStore_inode,iNum,&parentInode)) || (0==block_store_read(fs->BlockStore_whole, parentInode.directPointer[0],&parentDir))){
		return -8;
	}

	// check if the dir block is full of entries using bitmap
	// only 7 entries are allowed in a directory, 0 - 6 bit, not including 7
	size_t available = 7;
	bitmap_t *bmp = bitmap_overlay(8,&(parentInode.vacantFile));
	available = bitmap_ffz(bmp);
	//printf("path: %s\ndirPath: %s\navailable: %lu\n\n",path,dirPath,available);
	if(available == SIZE_MAX || available == 7){
		bitmap_destroy(bmp);
		return -9;
	} 
	bitmap_set(bmp, available);
	bitmap_destroy(bmp);

	// allocate a new inode for the new file and get its inode number
	size_t newInodeID = block_store_sub_allocate(fs->BlockStore_inode);
	// create a new inode for the file
	inode_t newInode;
	newInode.fileType = fileType;
	if(fileType == 'd'){ // If create a directory
		newInode.vacantFile = 0x00;
		directoryBlock_t newDirBlock;
		if(SIZE_MAX == block_store_allocate(fs->BlockStore_whole)){
			return -12;
		} 
		// allocate a block for the directory entries
		newInode.directPointer[0] = block_store_allocate(fs->BlockStore_whole);
		block_store_write(fs->BlockStore_whole, newInode.directPointer[0],&newDirBlock);
		newInode.fileSize = BLOCK_SIZE_BYTES;	
	} else { // If to create a file
		newInode.fileSize = 0;
		// Not need to allocate an empty block for the file
		
	}
	newInode.inodeNumber = newInodeID;
	newInode.linkCount = 1;
	// write the created inode to the inode table
	block_store_inode_write(fs->BlockStore_inode,newInodeID,&newInode);
	
	// write it to the block store
	if(64 != block_store_inode_write(fs->BlockStore_inode, parentInode.inodeNumber, &parentInode)){
		return -10;
	}
	// add a new entry of filename and inode number to the parentDir 
	directoryFile_t df;
	strcpy(df.filename, baseFileName);
	df.inodeNumber = newInodeID; 
	//printf("baseFileName: %s inodeNumber: %d\n", baseFileName, newInodeID);
	parentDir.dentries[available] = df;
	if(0==block_store_write(fs->BlockStore_whole,parentInode.directPointer[0],&parentDir)){
		return -11;
	}
	
	// printf("dirPath: %s \n baseName: %s\n",dirPath, baseFileName);
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
	fd_t.locate_order = 0;
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
	if(!block_store_sub_test(fs->BlockStore_fd,fd)){return -2;} // error if fd is not allocated
	block_store_sub_release(fs->BlockStore_fd,fd); 
	return 0;
}

///
/// Populates a dyn_array with information about the files in a directory
///   Array contains up to 15 file_record_t structures
/// \param fs The F17FS containing the file
/// \param path Absolute path to the directory to inspect
/// \return dyn_array of file records, NULL on error
///
dyn_array_t *fs_get_dir(F17FS_t *fs, const char *path){
	if(fs == NULL || path == NULL || strlen(path) < 1){
		return NULL;
	}

	// validate the pathname
	// valid path must start with '/'
	char firstChar = *path;
	if(firstChar != '/'){return NULL;}
	// parse the pathname
	char dirc[strlen(path)+1]; 
	char basec[strlen(path)+1];
	strcpy(dirc,path);
	strcpy(basec,path);
	char *dirPath = dirname(dirc);
	char *baseFileName = basename(basec);


	size_t dirInodeID;
	// if the directory is the root
	if(strlen(path)==1 && 0==strncmp(path,"/",1)){
		dirInodeID = 0; // set the inode number to 0 for root
	} else{ // other wise, trace down from the root to look for the inode
		// get the inode number of the target directory
		size_t parentInodeID;
		if((parentInodeID=searchPath(fs,dirPath)) == SIZE_MAX) {return NULL;}
		dirInodeID = getFileInodeID(fs,parentInodeID,baseFileName);
		if(dirInodeID == 0){return NULL;} // No such file is found, if it is not root, the inode number cannot be 0
	}
	// get the inode block and data block of the directory
	inode_t dirInode;
	directoryBlock_t dirBlock;
	if((0 == block_store_inode_read(fs->BlockStore_inode, dirInodeID, &dirInode)) || (0 == block_store_read(fs->BlockStore_whole,dirInode.directPointer[0],&dirBlock))){ return NULL;}	
	if('d'!=dirInode.fileType){return NULL;} // Should be directory
	
	// create a dynamic array, data object size is sizeof(file_record_t)
	dyn_array_t *list = dyn_array_create(15,sizeof(file_record_t),NULL);
	if(list == NULL){
		return NULL;
	}
	// loop through all the allocated entries in the data block in form of directoryBlock_t structure
	// use bitmap to skip unused/uninitialized entires
	bitmap_t * bmp = bitmap_overlay(8,&(dirInode.vacantFile));
	int k = 0;
	for(;k<7;k++){
		if(bitmap_test(bmp,k)){
			// add the entry name to the array 
			file_record_t record;
			strncpy(record.name,dirBlock.dentries[k].filename,FS_FNAME_MAX);
			inode_t fileInode;
			if(0==block_store_inode_read(fs->BlockStore_inode,dirBlock.dentries[k].inodeNumber,&fileInode)){
				bitmap_destroy(bmp);
				dyn_array_destroy(list);
				return NULL;
			}else {
				if(fileInode.fileType == 'r'){
					record.type = FS_REGULAR;
				} else {
					record.type = FS_DIRECTORY;
				}
				
			}
			if(!dyn_array_push_back(list,&record)){
				bitmap_destroy(bmp);
				dyn_array_destroy(list);
				return NULL;
			}
			//printf("record name: %s\n",record.name);	
		}	
	}
	bitmap_destroy(bmp);	
	return list;
}

// allocate and get the data block id
// \param fs The F17FS Filesystem
// \param fd_t The fileDescriptor object
// \param fileInode Inode of the file
// return the data block id, or <0 on error
ssize_t get_data_block_id(F17FS_t *fs, fileDescriptor_t *fd_t){
	if(fs==NULL || fd_t==NULL){
		return -1;
	} else {
		inode_t *ino = malloc(sizeof(inode_t));
		if(0==block_store_inode_read(fs->BlockStore_inode,fd_t->inodeNum,ino)){
			free(ino);
			return -1;
		} else {
			size_t usedBlockCount = ceil(ino->fileSize/512);//get the blocks used by this file
			size_t order = fd_t->locate_order; 
	
			if(fd_t->usage == 1){ // the block to be used is pointed by directPointer
				if(order>=usedBlockCount){ // if the block hasnt been allocated
					if(1<=block_store_get_free_blocks(fs->BlockStore_whole)){
						ino->directPointer[order] = block_store_allocate(fs->BlockStore_whole);	
					} else {return -1;}	
				}
				//printf("direct block address: %lu\n",order);
				return ino->directPointer[order]; // return the pointer, i.e., the address of the data block to write
			} else if(fd_t->usage == 2){ // the block is pointed by indirectPointer
				uint16_t table[256]; // the index table of the indirectPointer
				memset(table,0x0000,256);
				if(order+6>=usedBlockCount){ // the block hasnt been allocated
					if(order==0){ // the block is the first indirectPointer pointed block
						// allocate a block for the index table pointed by the indirectPointer in the inode 
						if(1<=block_store_get_free_blocks(fs->BlockStore_whole)){
							ino->indirectPointer = block_store_allocate(fs->BlockStore_whole);
						} else {return -1;}
					} 
					block_store_read(fs->BlockStore_whole,ino->indirectPointer,table);
					if(1<=block_store_get_free_blocks(fs->BlockStore_whole)){
						table[order] = block_store_allocate(fs->BlockStore_whole); // allocate the data block pointed by an entry in the index table
							//printf("Indirectpointer : %lu\n",order);
				 	} else {return -1;}
					if(0==block_store_write(fs->BlockStore_whole,ino->indirectPointer,table)){ // update the index table
						return -2;
					} 
				} else { // the block is already allocated 
					if(0==block_store_read(fs->BlockStore_whole,ino->indirectPointer,table)){
						return -3;
					}
				}
				//printf("indirect block address: %d\n",table[count]);
				return table[order];	
			} else { // the block is pointed by a doubleIndiretPointer
				uint16_t outerIndexTable[256];
				memset(outerIndexTable,0x0000,256);
				uint16_t innerIndexTable[256];
				memset(innerIndexTable,0x0000,256);
				if(order+256+6>=usedBlockCount){ //the block hasnt been allocated yet
					if(order==0){ // when the new block is the first entry of the first innerIndexTable of the outerIndexTable pointed by doubleIndirectPointe
						//printf("outerIndexTable index: %lu,usedBlockCount: %lu\n",order/256,usedBlockCount);
						if(3<=block_store_get_free_blocks(fs->BlockStore_whole)){
							ino->doubleIndirectPointer = block_store_allocate(fs->BlockStore_whole);
							outerIndexTable[0] = block_store_allocate(fs->BlockStore_whole);
							block_store_write(fs->BlockStore_whole,ino->doubleIndirectPointer,outerIndexTable);	
							innerIndexTable[0] = block_store_allocate(fs->BlockStore_whole);
							block_store_write(fs->BlockStore_whole,outerIndexTable[0],innerIndexTable);
						} else {
							return -4;
						}
					} else if(order%256==0){ // when the new block is the first entry of a new innerIndexTable
						if(2<=block_store_get_free_blocks(fs->BlockStore_whole)){
							//printf("outerIndexTable index: %lu,usedBlockCount: %lu\n",order/256,usedBlockCount);
							block_store_read(fs->BlockStore_whole,ino->doubleIndirectPointer,outerIndexTable);
							outerIndexTable[order/256] = block_store_allocate(fs->BlockStore_whole);
							block_store_write(fs->BlockStore_whole,ino->doubleIndirectPointer,outerIndexTable);	
							innerIndexTable[0] = block_store_allocate(fs->BlockStore_whole);
							block_store_write(fs->BlockStore_whole,outerIndexTable[order/256],innerIndexTable);	
						} else {
							return -5;
						}
					} else {// when the new block is in the same innerIndexTable
						if(1<=block_store_get_free_blocks(fs->BlockStore_whole)){
							block_store_read(fs->BlockStore_whole,ino->doubleIndirectPointer,outerIndexTable);	
							block_store_read(fs->BlockStore_whole,outerIndexTable[order/256],innerIndexTable);
							innerIndexTable[order%256] = block_store_allocate(fs->BlockStore_whole);	
							block_store_write(fs->BlockStore_whole,outerIndexTable[order/256],innerIndexTable);
						} else {
							return -6;
						}
					}
				} else { // the block is already allocated
					block_store_read(fs->BlockStore_whole,ino->doubleIndirectPointer,outerIndexTable);	
					block_store_read(fs->BlockStore_whole,outerIndexTable[order/256],innerIndexTable);
				}
				block_store_inode_write(fs->BlockStore_inode,fd_t->inodeNum,ino);//update the inode
				free(ino);
				//printf("double indirect block address: %d\n",innerIndexTable[count]);
				return innerIndexTable[order%256];
			}
		}		
	} 
}

//
// calculate the file size up until the location pointed by fileDescriptor usage, order and offset
// return size of the file
size_t getFileSize(fileDescriptor_t *fd_t){
		if(fd_t->usage == 1){
			return 512 * fd_t->locate_order + fd_t->locate_offset;
		} else if(fd_t->usage == 2){
			return 512 * (6 + fd_t->locate_order) + fd_t->locate_offset;	
		} else {
			return 512 * (6 + 256 + fd_t->locate_order) + fd_t->locate_offset;
		}
} 


/// Writes data from given buffer to the file linked to the descriptor
///   Writing past EOF extends the file
///   Writing inside a file overwrites existing data
///   R/W position is incremented by the number of bytes written
/// \param fs The F17FS containing the file/// \param fd The file to write to
/// \param dst The buffer to read from
/// \param nbyte The number of bytes to write
/// \return number of bytes written (< nbyte IF out of space), < 0 on error
///
ssize_t fs_write(F17FS_t *fs, int fd, const void *src, size_t nbyte){
	// check if fs,fd,src are valid
	if(fs==NULL || fd < 0 || !block_store_sub_test(fs->BlockStore_fd,fd) || src == NULL){
		return -1;
	} else {
		// if 0 byte is needed to write
		if(nbyte==0){
			return 0;
		} else {
			// get fd's corresponding fileDescriptor structure 
			// get inode number, usage, order and offset
			fileDescriptor_t fd_t;
			if(0==block_store_fd_read(fs->BlockStore_fd,fd,&fd_t)){
				return -2;
			} else {
				//printf("Free blocks: %lu\n",block_store_get_free_blocks(fs->BlockStore_whole));
				// File size up until the fd pointer
				size_t locSize = getFileSize(&fd_t);
				// calculate number of data blocks needed to store the src data including the block the offset is at
				size_t blocksNeeded = ceil((nbyte+fd_t.locate_offset)/512.0);
				// get the number of available blocks 
				size_t freeBlocks = block_store_get_free_blocks(fs->BlockStore_whole);
				if(freeBlocks == SIZE_MAX){
					return -3;
				} else {
					if(blocksNeeded>freeBlocks){
					blocksNeeded = freeBlocks;
					}
					
					size_t writtenBytes = 0;
					// write data to the first block starting where the current fd pointer is at
					size_t i=0;	
					for(;i<blocksNeeded;i++){
						//if(200 >= block_store_get_free_blocks(fs->BlockStore_whole)){
						//	printf("Free blocks: %lu\n",block_store_get_free_blocks(fs->BlockStore_whole));
						//}
						if(0<block_store_get_free_blocks(fs->BlockStore_whole)){ 
							ssize_t blockID = get_data_block_id(fs,&fd_t); // get the block id where the fd offset is at
							if(blockID>=0){ 
								if(i==0&&fd_t.locate_offset>0){ // the first block to write where the offset is not 0
									if(0!=block_store_append(fs->BlockStore_whole,blockID,fd_t.locate_offset,src+writtenBytes)){
										if(nbyte+fd_t.locate_offset<512){// if the number of bytes is smaller than the remaining bytes in the block 
											writtenBytes += nbyte;
											fd_t.locate_offset += nbyte;// update offset but not order
											// only update offset when it's the last write 
											break; // finishi writing
										} else {
											writtenBytes += (512-fd_t.locate_offset);
										}	
									} else {return -5;}
								} else {
									if(0!=block_store_write(fs->BlockStore_whole,blockID,src+writtenBytes)){
										if(nbyte-writtenBytes>=512){
											writtenBytes += 512;
										}else{// if the remaining bytes is less than a block
											fd_t.locate_offset += (nbyte-writtenBytes);//update
											// only update offset when it's the last write 
											writtenBytes += (nbyte-writtenBytes);
											break;	
										}	
									} else {
										//printf("blockID: %ld\n",blockID);
										//printf("freeBlocks: %lu\n",block_store_get_free_blocks(fs->BlockStore_whole));
										return -6; 
									}
								}
								// update locate_order
								if(fd_t.usage==1&&fd_t.locate_order== (DIRECT_BLOCKS-1)){ // directPointer space is full
									fd_t.locate_order = 0;
									fd_t.usage = 2;
									//printf("dip into indirect blocks\n");
								} else if(fd_t.usage==2&&fd_t.locate_order== (INDIRECT_BLOCKS-1)){ // indirectPointer space is full
									fd_t.locate_order = 0;
									fd_t.usage = 4;
									//printf("dip into double indirect blocks\n");
								} else { // as long as not exceeding the double inidirect max, unlikely
									fd_t.locate_order += 1;
								}
									
							} else {
								break;
							}	
						} else {
							break;
						}
					}
					inode_t fileInode;
					block_store_inode_read(fs->BlockStore_inode,fd_t.inodeNum,&fileInode);
					if(fileInode.fileSize < locSize + writtenBytes){ // Need to recalculate
						fileInode.fileSize = locSize + writtenBytes;
					}
					if(0!=block_store_fd_write(fs->BlockStore_fd,fd,&fd_t) && 0!=block_store_inode_write(fs->BlockStore_inode,fd_t.inodeNum,&fileInode)){
					return writtenBytes;
					} else {return -8;}							

				}	
			}
		}
	}
}

//
// Deletes the specified file and closes all open descriptors to the file
//   Directories can only be removed when empty
// \param fs The F17FS containing the file
// \param path Absolute path to file to remove
// \return 0 on success, < 0 on error
//
int fs_remove(F17FS_t *fs, const char *path) {
	if(fs == NULL || path == NULL || strlen(path) == 0){
		return -1;
	}
	// Validate the path
	char firstChar = *path;
	if(firstChar != '/'){return -2;}
	char dirc[strlen(path)+1];
	char basec[strlen(path)+1];
	strcpy(dirc,path);
	strcpy(basec,path);
	char *dirPath =  dirname(dirc);
	char *baseFileName = basename(basec);

	size_t dirInodeID = searchPath(fs,dirPath);
	size_t fileInodeID;
	if(dirInodeID != SIZE_MAX){
		if(0==strcmp(baseFileName,"/") && 0==strcmp(dirPath,"/") ){
			fileInodeID = dirInodeID;
		} else {
			fileInodeID = getFileInodeID(fs,dirInodeID,baseFileName);
			if(fileInodeID == 0){
				printf("dirInodeID: %lu\n",dirInodeID);
				printf("invalid fileInodeID %lu for dir: %s file: %s\n",fileInodeID,dirPath,baseFileName);
				return -4;
			}
		}
		inode_t fileInode;
		block_store_inode_read(fs->BlockStore_inode,fileInodeID,&fileInode);
		if(fileInode.fileType=='d'){
		// If the file is a dir, delete it only if it is empty, meaning vacantFile == 0x00
			if(fileInode.vacantFile == 0x00){
				// To delete a dir, remove its entry from the directory block of its parent inode
				inode_t dirInode; // Parent directoy inode
				block_store_inode_read(fs->BlockStore_inode,dirInodeID,&dirInode);
				//printf("Dir %s before clear file,vacantFile: %d\n",path,dirInode.vacantFile); 
				bitmap_t *bmp = bitmap_overlay(8, &(dirInode.vacantFile));
				directoryBlock_t db_t;
				block_store_read(fs->BlockStore_whole,dirInode.directPointer[0],&db_t); 	
				int m=0;
				for(;m<7; m++){
					if(bitmap_test(bmp,m)){
						if(fileInodeID == db_t.dentries[m].inodeNumber /* && 0 < parentDir.dentries[k].inodeNumber*/){
							bitmap_reset(bmp,m);	
							break;
						}
					}	
				}
				//printf("Dir %s after clear file,vacantFile: %d\n",path,dirInode.vacantFile); 
				bitmap_destroy(bmp);
				block_store_inode_write(fs->BlockStore_inode,dirInodeID,&dirInode);
				// Remove its file block pointed by directPointer[0], then remove its inode from inode table
				block_store_release(fs->BlockStore_whole,fileInode.directPointer[0]);
				block_store_sub_release(fs->BlockStore_inode,fileInodeID);
				return 0;
			}
			//printf("Dir %s not empty,vacantFile: %d\n",path,fileInode.vacantFile); 
			return -5;
		} else if(fileInode.fileType=='r') { // if the file to remove is file
			int i=0;
			for(; i<6; i++){ // if the directPointer is allocated, release those memory addresses first
				if(block_store_test(fs->BlockStore_whole,fileInode.directPointer[i])){
					block_store_release(fs->BlockStore_whole,fileInode.directPointer[i]);	
				}	
			}
			if(block_store_test(fs->BlockStore_whole,fileInode.indirectPointer)){// if the indirectPointer is allocated, 
				uint16_t indexTable[256];
				if(block_store_read(fs->BlockStore_whole,fileInode.indirectPointer,indexTable)){
					int j=0;
					for(; j<256; j++){ // release all the secondary memory addresses 
						if(indexTable[j]!=0x0000 && block_store_test(fs->BlockStore_whole,indexTable[j])){
							block_store_release(fs->BlockStore_whole,indexTable[j]);	
						}	
					}
				}
				block_store_release(fs->BlockStore_whole,fileInode.indirectPointer);	
			}
			if(block_store_test(fs->BlockStore_whole,fileInode.doubleIndirectPointer)){// if the doubleIndirectPointer is allocated, 
				uint16_t outerIndexTable[256];
				uint16_t innerIndexTable[256];
				if(block_store_read(fs->BlockStore_whole,fileInode.doubleIndirectPointer,outerIndexTable)){
					int j=0;
				for(; j<256; j++){ // release all the secondary memory addresses 
						if(outerIndexTable[j]!=0x0000 && block_store_test(fs->BlockStore_whole,outerIndexTable[j])){			
							if(block_store_read(fs->BlockStore_whole,outerIndexTable[j],innerIndexTable)){
								int k=0;
								for(; k<256; k++){ // release all the secondary memory addresses 
									if(innerIndexTable[k]!=0x0000 && block_store_test(fs->BlockStore_whole,innerIndexTable[k])){
										block_store_release(fs->BlockStore_whole,innerIndexTable[k]);	
									}	
								}
							}
							block_store_release(fs->BlockStore_whole,outerIndexTable[j]);	
						}	
					}
				}
				block_store_release(fs->BlockStore_whole,fileInode.doubleIndirectPointer);	
			}
			int fd_count=0;
			for(;fd_count<256;fd_count++){ // close all fd pointing to the file
				if(block_store_sub_test(fs->BlockStore_fd,fd_count)){
					fileDescriptor_t fd_t;
					if(0!=block_store_fd_read(fs->BlockStore_fd,fd_count,&fd_t)){
						if(fd_t.inodeNum==fileInodeID){
							fs_close(fs,fd_count);
						}
					}
					return -7;
				}
			}

			// Remduce one bit from vacantFile
			inode_t dirInode; // The inode for the parent directory containing the file
			block_store_inode_read(fs->BlockStore_inode,dirInodeID,&dirInode);
			//dirInode.vacantFile >>= 1 ; // Cannot do this, would mess up the directory file block
			//printf("Dir %s before clear file,vacantFile: %d\n",path,dirInode.vacantFile); 
			bitmap_t *bmp = bitmap_overlay(8, &(dirInode.vacantFile));
			directoryBlock_t db_t;
			block_store_read(fs->BlockStore_whole,dirInode.directPointer[0],&db_t); 	
			int m=0;
			for(;m<7; m++){
				if(bitmap_test(bmp,m)){
					if(fileInodeID == db_t.dentries[m].inodeNumber /* && 0 < parentDir.dentries[k].inodeNumber*/){
						bitmap_reset(bmp,m);	
						break;
					}
				}
			}
			//printf("Dir %s after clear file,vacantFile: %d\n",path,dirInode.vacantFile); 
			bitmap_destroy(bmp);
			block_store_inode_write(fs->BlockStore_inode,dirInodeID,&dirInode);
			return 0;
		}
		return -6;	
	}
	return -3;
	// To delete a file, you need to search all the data blocks allocated to it, including direct, indirect and dbindirect blocks.
}




