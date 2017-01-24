#include "procsim.hpp"
#include <stdlib.h>

/**
 * Subroutine for initializing the processor. You many add and initialize any global or heap
 * variables as needed.
 * XXX: You're responsible for completing this routine
 *
 * @r Number of result buses
 * @k0 Number of k0 FUs
 * @k1 Number of k1 FUs
 * @k2 Number of k2 FUs
 * @f Number of instructions to fetch
 * @e Exception cycle rate
 * @s Exception repair scheme
 */

uint64_t R;
uint64_t F;
uint64_t J;
uint64_t K;
uint64_t L;
uint64_t E;
uint64_t S;

struct ScheQ* RS_head;
struct ScheQ* RS_tail;
int rsize;
int freeRS;

int tag_count=0;

struct CompQ* CPQ_head;
struct CompQ* CPQ_tail;
struct CompQ* CPQ_mid;
struct CompQ* CPQ_ahead_mid;

bool setmid;

int freecpq[3];

struct ScheQ** retire;

struct ROB* robhead;
struct ROB* robtail;

struct ScheQ* refetchinst;
struct ScheQ* refetch_tail;

struct RegFile* RF;

int DispQ_length=0;
bool nowexcept = false;
int exceptpc = 1;
int exceptcycle = 0;
int last_exceptisnt = -1;
int exceptbarri = 0;

#define RFSIZE 128

struct InsRecord* recordhead;
struct InsRecord* recordtail;

struct RegFile* messy;
int barrier = 20;
int old_barrier = 0;

int reghits = 0;
int robhits = 0;
int backups = 0;
int flushes = 0;
int fireds = 0;

struct DispQ* Dhead = NULL;
struct DispQ* Dtail = NULL;

void setup_proc(uint64_t r, uint64_t k0, uint64_t k1, uint64_t k2, uint64_t f, uint64_t e, uint64_t s) 
{
	R=r;
	J=k0;
	K=k1;
	L=k2;
	F=f;
	E=e;
	S=s;

	rsize = 2*(J+K+L);
	freeRS = rsize;
	RS_head = NULL;
	RS_tail = NULL;

	RF = new RegFile[RFSIZE];
	for(int i=0;i<RFSIZE;i++){
		RF[i].ready = true;     
		RF[i].tag = -1;
	}

	messy = new RegFile[RFSIZE];
	for(int i=0;i<RFSIZE;i++){
		messy[i].ready = true;     
		messy[i].tag = -1;
	}

	CPQ_head = NULL;
	CPQ_tail = NULL;
	CPQ_mid = NULL;
	CPQ_ahead_mid = NULL;

	freecpq[0] = J;
	freecpq[1] = K;
	freecpq[2] = L;

	retire = new ScheQ*[R];
	for(int i=0;i<R;i++){
		retire[i] = NULL;
	}

	robhead=NULL;
	robtail=NULL;

	refetchinst = NULL;
	refetch_tail = NULL;

	recordhead=NULL;
	recordtail=NULL;


}

/**
 * Subroutine that simulates the processor.
 *   The processor should fetch instructions as appropriate, until all instructions have executed
 * XXX: You're responsible for completing this routine
 *
 * @p_stats Pointer to the statistics structure
 */


bool addto_RS(proc_inst_t inst,int32_t inst_num){

	if(freeRS==0){
		return false;
	}else{

		if(RS_head==NULL){
			RS_head = new ScheQ;
			RS_tail = RS_head;
			RS_tail->next = NULL;
		}else{
			
			ScheQ* tmp = new ScheQ;
			RS_tail->next = tmp;
			RS_tail = tmp;
			RS_tail->next = NULL;
		}

		RS_tail->func = inst.op_code;
		RS_tail->src0_reg = inst.src_reg[0];
		RS_tail->src1_reg = inst.src_reg[1];


		if(inst.src_reg[0]>=0){
			//----
			ROB* robtmp=robhead;
			bool flag = false;
			while(robtmp!=NULL){
				if(robtmp->reg == inst.src_reg[0]){
					RS_tail->src0_ready = robtmp->completed;
					RS_tail->src0_tag = robtmp->tag;
					flag = true;
				}
				robtmp = robtmp->next;
			}
			//-----
			if(flag == false){
				RS_tail->src0_ready = true;
				reghits++;
			}else{
				robhits++;
			}		
		}else{
			RS_tail->src0_ready = true;
		}

		if(inst.src_reg[1]>=0){
			//----
			ROB* robtmp=robhead;
			bool flag = false;
			while(robtmp!=NULL){
				if(robtmp->reg == inst.src_reg[1]){
					RS_tail->src1_ready = robtmp->completed;
					RS_tail->src1_tag = robtmp->tag;
					flag = true;
				}
				robtmp = robtmp->next;
			}
			//-----
			if(flag == false){
				RS_tail->src1_ready = true;
				reghits++;
			}else{
				robhits++;
			}
			
		}else{
			RS_tail->src1_ready = true;
		}

		RS_tail->dest_reg = inst.dest_reg;

		if(RS_tail->dest_reg<0){
			RS_tail->dest_reg_tag = -1;
		}else{
			RS_tail->dest_reg_tag = tag_count;

			tag_count++;
		}

		RS_tail->inst_num = inst_num;

		RS_tail->intoFU = false;

		freeRS--;
		return true;
	}

}


void addto_CPQ(struct ScheQ* theRS){


	if(CPQ_head==NULL){
		CPQ_head = new CompQ;
		CPQ_tail = CPQ_head;
		CPQ_tail->theRS = theRS;
		CPQ_tail->next = NULL;

		CPQ_mid = CPQ_tail;
	}else{
		CompQ* tmp = new CompQ;
		tmp->theRS = theRS;
		tmp->next = NULL;

		if(setmid){
			CPQ_ahead_mid = CPQ_tail;
			CPQ_mid = tmp;
			CPQ_tail->next = tmp;
			CPQ_tail = tmp;
		}else{
			CompQ* p1 = CPQ_ahead_mid;
			CompQ* p2 = CPQ_mid;
			bool flag=false;
			while(p2!=NULL){
				if((theRS->dest_reg_tag) < (p2->theRS->dest_reg_tag)){
					p1->next=tmp;
					tmp->next=p2;
					flag = true;
					break;
				}
				//both tag==-1?
				p1=p2;
				p2=p2->next;
			}
			if(flag==false){
				p1->next = tmp;
				tmp->next = NULL;
			}
		}


	}

}

struct ScheQ* removfrom_CPQ(){
	if(CPQ_head!=NULL){
		ScheQ* theRS = CPQ_head->theRS;
		CompQ* tmp = CPQ_head->next;
		delete CPQ_head;
		CPQ_head = tmp;
		return theRS;
	}else{
		return NULL;
	}
}



void addto_ROB(){  //the fresh tail of RS
	if(robhead==NULL){
		robhead = new ROB;
		robtail = robhead;
		robtail->inst_num = RS_tail->inst_num;
		robtail->reg = RS_tail->dest_reg;
		robtail->tag = RS_tail->dest_reg_tag;

		robtail->completed = false;
		robtail->retired = false;
		robtail->exception = false;
		robtail->next = NULL;
	}else{
		robtail->next = new ROB;
		robtail = robtail->next;
		robtail->inst_num = RS_tail->inst_num;
		robtail->reg = RS_tail->dest_reg;
		robtail->tag = RS_tail->dest_reg_tag;

		robtail->completed = false;
		robtail->retired = false;
		robtail->exception = false;
		robtail->next = NULL;
	}
}

ROB* getrob(int inst_num){
	ROB* tmp=robhead;
	while(tmp!=NULL){
		if(tmp->inst_num==inst_num){
			return tmp;
		}
		tmp=tmp->next;
	}
	return NULL;
}




void exception_handler(){
	
	//printf("handler haha\n");
	ScheQ* tmp;
	//retire those in RS
	while(RS_head!=NULL){
		if(RS_head->inst_num < exceptpc){
			tmp = RS_head;
			RS_head = RS_head->next;
			delete tmp;	
		}else{
			break;
		}
	}
	//copy current RS to refetchinst. delete RS
	while(RS_head!=NULL){
		flushes++;
		if(refetchinst==NULL){
			refetchinst = new ScheQ;
			refetch_tail = refetchinst;
			refetch_tail->next = NULL;
		}else{
			refetch_tail->next = new ScheQ;
			refetch_tail = refetch_tail->next;
			refetch_tail->next = NULL;
		}
		refetch_tail->inst_num = RS_head->inst_num;
		refetch_tail->src0_reg = RS_head->src0_reg;
		refetch_tail->src1_reg = RS_head->src1_reg;
		refetch_tail->dest_reg = RS_head->dest_reg;
		refetch_tail->func = RS_head->func;
		tmp = RS_head;
		RS_head = RS_head->next;
		delete tmp;
	}
	RS_tail=NULL;
	freeRS = rsize;
	//delete dispq

	DispQ_length=0;
	
	//delete compq
	while(removfrom_CPQ()!=NULL){

	}
	CPQ_tail = NULL;
	//delete rob
	ROB* rtmp;
	while(robhead!=NULL){
		rtmp = robhead;
		robhead = robhead->next;
		delete rtmp;
	}
	robtail=NULL;

	freecpq[0] = J;
	freecpq[1] = K;
	freecpq[2] = L;

}

//-------------------------------------------------------------------------------------------------------------------------------

void exception_handler_cpr(){
	//copy current Record Inst to refetchinst. 
	//printf("handler cpr haha\n");
	
	InsRecord* tmp;
	while(recordhead!=NULL){
		flushes++;
		if(refetchinst==NULL){
			refetchinst = new ScheQ;
			refetch_tail = refetchinst;
			refetch_tail->next = NULL;
		}else{
			refetch_tail->next = new ScheQ;
			refetch_tail = refetch_tail->next;
			refetch_tail->next = NULL;
		}
		refetch_tail->inst_num = recordhead->inst_num;
		refetch_tail->src0_reg = recordhead->src0_reg;
		refetch_tail->src1_reg = recordhead->src1_reg;
		refetch_tail->dest_reg = recordhead->dest_reg;
		refetch_tail->func = recordhead->func;
		tmp = recordhead;
		recordhead = recordhead->next;
		delete tmp;
	}
	recordtail=NULL;
	//printf("handler cpr haha\n");

	//delete RS
	freeRS = rsize;
	ScheQ* stmp;
	while(RS_head!=NULL){
		stmp=RS_head;
		RS_head = RS_head->next;
		delete stmp;
	}
	//delete dispq
	//printf("handler cpr haha\n");

	DispQ_length=0;
	
	//delete compq
	while(removfrom_CPQ()!=NULL){

	}
	CPQ_tail = NULL;
	freecpq[0] = J;
	freecpq[1] = K;
	freecpq[2] = L;
	//printf("handler cpr haha\n");

	//set messy ready = true;
	for(int i=0;i<RFSIZE;i++){
		messy[i].ready = true;     
		messy[i].tag = -1;
	}
	//printf("handler cpr haha\n");

}

bool addto_RS_cpr(proc_inst_t inst,int32_t inst_num){

	if(freeRS==0){
		return false;
	}else{
		if(RS_head==NULL){
			RS_head = new ScheQ;
			RS_tail = RS_head;
			RS_tail->next = NULL;
		}else{
			
			ScheQ* tmp = new ScheQ;
			RS_tail->next = tmp;
			RS_tail = tmp;
			RS_tail->next = NULL;
		}
		RS_tail->func = inst.op_code;
		RS_tail->src0_reg = inst.src_reg[0];
		RS_tail->src1_reg = inst.src_reg[1];

		if(inst.src_reg[0]>=0){
			RS_tail->src0_ready = messy[inst.src_reg[0]].ready;
			RS_tail->src0_tag = messy[inst.src_reg[0]].tag;	
			reghits++;
		}else{
			RS_tail->src0_ready = true;	

		}

		if(inst.src_reg[1]>=0){
			RS_tail->src1_ready = messy[inst.src_reg[1]].ready;
			RS_tail->src1_tag = messy[inst.src_reg[1]].tag;	
			reghits++;			
		}else{
			RS_tail->src1_ready = true;	
			
		}

		RS_tail->dest_reg = inst.dest_reg;

		if(RS_tail->dest_reg<0){
			RS_tail->dest_reg_tag = -1;
		}else{
			RS_tail->dest_reg_tag = tag_count;
			messy[inst.dest_reg].ready = false;
			messy[inst.dest_reg].tag = tag_count;
			tag_count++;
		}

		RS_tail->inst_num = inst_num;

		RS_tail->intoFU = false;

		freeRS--;
		return true;
	}

}

void addto_Record(){

	if(recordhead==NULL){
		recordhead = new InsRecord;
		recordtail = recordhead;
		recordtail->next = NULL;

	}else{
		recordtail->next = new InsRecord;
		recordtail = recordtail->next;
		recordtail->next = NULL;
	}
	recordtail->fetch=0;
	recordtail->disp=0;
	recordtail->sched=0;
	recordtail->exec=0;
	recordtail->state=0;
	recordtail->finished=false;

	recordtail->dest_reg = RS_tail->dest_reg;
	recordtail->src0_reg = RS_tail->src0_reg;
	recordtail->src1_reg = RS_tail->src1_reg;
	recordtail->func = RS_tail->func;

	recordtail->inst_num = RS_tail->inst_num;

}

InsRecord* get_Record(int inst_num){
	InsRecord* tmp = recordhead;
	while(tmp!=NULL){
		if(tmp->inst_num == inst_num){
			return tmp;
		}
		tmp = tmp->next;
	}
	return NULL;
}



//-------------------------------------------------------------------------------------------------------------------------

int fetch_pc=0;
int dsize = 0;
int old_dsize = 0;
int max_size = 0;

void run_proc(proc_stats_t* p_stats)
{
	proc_inst_t inst;

	int PC = 1;

	int32_t ff;
	ScheQ* rs1 = NULL;
	ScheQ* rs2 = RS_head;
	ScheQ* rebus[R];
	
	p_stats->exception_count = 0;
	p_stats->retired_instruction = 0;
	

	bool non_read = false;
	printf("INST\tFETCH\tDISP\tSCHED\tEXEC\tSTATE\n");

	if(S==1){

		for(int cycle=1;cycle>0;cycle++){

			//printf("*********************\ncycle %d\n",cycle);
			p_stats->cycle_count++;
			//-------------handle exception mark ROB retire----------
			ROB* robtmp=robhead;
			while(robtmp!=NULL){
				if(robtmp->completed){
					if(!robtmp->exception){              
						robtmp->retired = true;
						robtmp->state = cycle;
						p_stats->retired_instruction++;
						printf("%d\t%d\t%d\t%d\t%d\t%d\n",
	 					robtmp->inst_num,robtmp->fetch,robtmp->disp,robtmp->sched,robtmp->exec,robtmp->state);
					}else{
						//next cycle ----> exception handler
						exceptpc = robtmp->inst_num;
						exceptcycle = cycle;
						nowexcept = true;

						fetch_pc = exceptpc - 1;
						dsize = 0;
						// printf("#########exception#########\n");
						// printf("cycle%d fetchpc%d dsize%d\n",cycle,fetch_pc,dsize);
						exception_handler();
						p_stats->exception_count++;
						break;
					}
				}else{
					break;
				}
				robtmp = robtmp->next;
			}
			//printf("mark retire\n");
			if(nowexcept){
				nowexcept = false;
				continue;
			}

			//-------------------------------------------------------
			
			//------------------exec (put on bus and rob completed)------------------
			

			for(int i=0;i<R;i++){
				rebus[i] = NULL;
				rebus[i] = removfrom_CPQ();
				if(rebus[i]==NULL){
					break;
				}

				ff = abs(rebus[i]->func);
				freecpq[ff]++;

				int32_t dest_reg = rebus[i]->dest_reg;
				int32_t dest_reg_tag = rebus[i]->dest_reg_tag;

				int32_t inst_num = rebus[i]->inst_num;

				if(dest_reg>=0 && RF[dest_reg].tag == dest_reg_tag){
					RF[dest_reg].ready = true;
					//printf("inst%d dest reg%d\n",inst_num,dest_reg);
				}

				//mark complete in rob and exception in rob
				ROB* robtmp=robhead;
				while(robtmp!=NULL){

					if(robtmp->inst_num == rebus[i]->inst_num){
						robtmp->completed = true;
						if(robtmp->inst_num % E == 0 && robtmp->inst_num != last_exceptisnt){
							robtmp->exception = true;
							last_exceptisnt = robtmp->inst_num;
						}
						break;
					}
					robtmp = robtmp->next;
				}

			}
			//printf("mark complete\n");
			//-------------------------------------------

			//-------------------enter FU (addto_CompQ) (+1 = exec)--------------------

			rs1 = NULL;
			rs2 = RS_head;

			int flag=0;
			while(rs2!=NULL){
				if((rs2->src0_ready) && (rs2->src1_ready) && (!rs2->intoFU)){

					ff = abs(rs2->func);

					if(freecpq[ff]>0){
						rs2->intoFU = true;

						if(flag==0){
							setmid = true;
						}
						addto_CPQ(rs2);
						fireds++;

						flag++;

						freecpq[ff]--;
						ROB* tmprecord = getrob(rs2->inst_num);
						tmprecord->exec = cycle+1;

					}
				}
				rs2 = rs2->next;
			}

			//-------------------------------------------------------------------------


			//---------------------update RS (mark ready)------------------

			for(int i=0;i<R;i++){
				if(rebus[i]==NULL){
					break;
				}
				ScheQ* tmp = RS_head;
				
				while(tmp!=NULL){
					
					if(tmp->src0_ready == false){
						if(tmp->src0_tag == rebus[i]->dest_reg_tag){
							tmp->src0_ready = true;
						}
					}
					if(tmp->src1_ready == false){
						if(tmp->src1_tag == rebus[i]->dest_reg_tag){
							tmp->src1_ready = true;
						}
					}
					
					tmp = tmp->next;
			
				}

			}
			//printf("update RS\n");
			//-------------------------------------------------------------------------


			//--------------enter RS (+1 =sched)-----------------------

			old_dsize = dsize;

			while(freeRS>0){
				if((DispQ_length>0)){

					if(refetchinst !=NULL){
						inst.instruction_address=0;
						inst.src_reg[0] = refetchinst->src0_reg;
						inst.src_reg[1] = refetchinst->src1_reg;
						inst.dest_reg = refetchinst->dest_reg;
						inst.op_code = refetchinst->func;

						PC = refetchinst->inst_num;
						addto_RS(inst,PC);
						addto_ROB();
						robtail->fetch = (robtail->inst_num - exceptpc)/F + exceptcycle + 1;
						robtail->disp = robtail->fetch + 1;
						robtail->sched = cycle + 1;

						DispQ_length--;
						dsize--;
						PC++;

						ScheQ* tmp = refetchinst;
						refetchinst = refetchinst->next;
						delete tmp;

					}else{
						if(read_instruction(&inst)){

							addto_RS(inst,PC);    //revised refer to rob first
							addto_ROB();
							robtail->fetch = (robtail->inst_num - exceptpc)/F + exceptcycle + 1;
							robtail->disp = robtail->fetch + 1;
							robtail->sched = cycle + 1;

							DispQ_length--;
							dsize--;
							PC++;
						}else {
							//dispq empty
							DispQ_length = -1;
							non_read = true;

						}
					}

				}else{
					break;
				}
			}

			
			//printf("enter RS\n");
			//---------------------------------------------------------

			//---------------------delete from RS----------------------
			robtmp=robhead;
			while(robtmp!=NULL){
				if(robtmp->retired){
					ScheQ* rs2 = RS_head;
					rs1 = NULL;
					while(rs2!=NULL){
						if(rs2->inst_num == robtmp->inst_num){
							if(rs1==NULL){
								RS_head=rs2->next;
								delete rs2;
								freeRS++;
								break;
							}else{
								rs1->next = rs2->next;
								delete rs2;
								freeRS++;
								break;
							}
						}
						rs1=rs2;
						rs2=rs2->next;
					}
				}else{
					break;
				}
				robtmp = robtmp->next;
			}
			
			while(robhead!=NULL){
				if(robhead->retired){
					robtmp = robhead;
					robhead = robhead->next;
					delete robtmp;
				}else{
					break;
				}
			}

			//printf("delete RS\n");
			//---------------------------------------------------------

			//--------------fetch (enter dispQueue)-----------------------
			//--------------disp (fetch + 1 = disp)-----------------------

			
			
			
			if(DispQ_length>=0){
				DispQ_length += F;
			}
			if(fetch_pc+F <= 100000){
				fetch_pc += F;
				dsize += F;
			}else{
				if(fetch_pc < 100000){
					dsize = dsize + 100000-fetch_pc;
					fetch_pc = 100000;
				}
			}

			p_stats-> avg_disp_size += old_dsize;
			if(old_dsize > max_size){
				max_size = old_dsize;
			}
			

			
			if((DispQ_length<0)&&(refetchinst==NULL)&&(RS_head==NULL)&&(CPQ_head==NULL)&&(robhead==NULL)){
				cycle=-10;
			}


		}
	}
	else if(S==2){
		for(int i=0;i<R;i++){
			rebus[i]=NULL;
			retire[i]=NULL;
		}

		for(int cycle=1;cycle>0;cycle++){
			//printf("*********************\ncycle %d\n",cycle);
			//---------------handle exceptions and mark retire----------------
			p_stats->cycle_count++;
			for(int i=0;i<R;i++){
				retire[i]=NULL;
				retire[i]=rebus[i];
				if(retire[i]==NULL){
					break;
				}
				//exception handle
				if(retire[i]->inst_num%E==0 && retire[i]->inst_num !=exceptpc){
					continue;
				}
				//mark retire and if reach barrier
				InsRecord* retmp = recordhead;
				int reach_barri_count=0;
				while(retmp!=NULL){
					if(retmp->inst_num == retire[i]->inst_num){
						retmp->finished = true;
						retmp->state =cycle;
					}
					if(retmp->finished && retmp->inst_num<=barrier){
						reach_barri_count++;
					}
					retmp = retmp->next;
				}
				//reach barrier
				if(reach_barri_count == (barrier - old_barrier)){ // reach barrier
					//copy IB1 to IB2 actually nothing to do
					backups++;
					//new barrier
					//printf("reach it!!!!\n");
					old_barrier = barrier;
					ScheQ* stmp = RS_head;
					barrier = 0;
					while(stmp!=NULL){
						if(stmp->inst_num>barrier){
							barrier = stmp->inst_num;
						}
						stmp = stmp->next;
					}
					
					while(recordhead!=NULL){
						if(recordhead->inst_num<=old_barrier){
							p_stats->retired_instruction++;
							printf("%d\t%d\t%d\t%d\t%d\t%d\n",
		 					recordhead->inst_num,recordhead->fetch,recordhead->disp,recordhead->sched,recordhead->exec,recordhead->state);
		 					retmp = recordhead;
		 					recordhead = recordhead->next;
		 					delete retmp;
	 					}else{
	 						break;
	 					}
					}

				}
	
			}

			for(int i=0;i<R;i++){
				if(retire[i]==NULL){
					break;
				}
				if(retire[i]->inst_num%E==0 && retire[i]->inst_num !=exceptpc){
						
						nowexcept = true;
						exceptpc = retire[i]->inst_num;
						exceptcycle = cycle;
						exceptbarri = old_barrier;
						
						fetch_pc = old_barrier;
						dsize = 0;
						exception_handler_cpr();
						p_stats->exception_count++;
						
						break;

				}
			}
			if(nowexcept){
				nowexcept = false;
				for(int j=0;j<R;j++){
					rebus[j] = NULL;
				}
				continue;
			}

			
			//------------result bus----------messy RF------------------------
			for(int i=0;i<R;i++){
				rebus[i] = NULL;
				rebus[i] = removfrom_CPQ();
				if(rebus[i]==NULL){
					break;
				}

				ff = abs(rebus[i]->func);
				freecpq[ff]++;
				int32_t dest_reg = rebus[i]->dest_reg;
				int32_t dest_reg_tag = rebus[i]->dest_reg_tag;
				int32_t inst_num = rebus[i]->inst_num;
				if(messy[dest_reg].tag == dest_reg_tag){
					messy[dest_reg].ready = true;
				}
				
			}
			
			//------------------------add to FU (+1 = exec)-------------------

			rs1 = NULL;
			rs2 = RS_head;

			int flag=0;
			while(rs2!=NULL){
				if((rs2->src0_ready) && (rs2->src1_ready) && (!rs2->intoFU)){

					ff = abs(rs2->func);

					if(freecpq[ff]>0){
						rs2->intoFU = true;

						if(flag==0){
							setmid = true;
						}
						addto_CPQ(rs2);
						fireds++;
						// printf("haha\n");
						InsRecord* intmp = get_Record(rs2->inst_num);
						intmp->exec = cycle+1;
						// printf("hahaxixi\n");
						flag++;

						freecpq[ff]--;
		
					}
				}
				rs2 = rs2->next;
			}

			//--------------------update RS----------------------------
			for(int i=0;i<R;i++){
				if(rebus[i]==NULL){
					break;
				}
				ScheQ* tmp = RS_head;
				
				while(tmp!=NULL){
					
					if(tmp->src0_ready == false){
						if(tmp->src0_tag == rebus[i]->dest_reg_tag){
							tmp->src0_ready = true;
						}
					}
					if(tmp->src1_ready == false){
						if(tmp->src1_tag == rebus[i]->dest_reg_tag){
							tmp->src1_ready = true;
						}
					}
					
					tmp = tmp->next;
			
				}

			}

			//---------------------slots RS-------------------------------
			old_dsize = dsize;
			while(freeRS>0){
				if((DispQ_length>0)){
					if(refetchinst !=NULL){
						inst.instruction_address=0;
						inst.src_reg[0] = refetchinst->src0_reg;
						inst.src_reg[1] = refetchinst->src1_reg;
						inst.dest_reg = refetchinst->dest_reg;
						inst.op_code = refetchinst->func;

						PC = refetchinst->inst_num;
						addto_RS_cpr(inst,PC);
						addto_Record();

						recordtail->fetch = (recordtail->inst_num - exceptbarri-1)/F + exceptcycle + 1;
						recordtail->disp = recordtail->fetch + 1;
						recordtail->sched = cycle + 1;   

						DispQ_length--;
						dsize--;
						PC++;

						ScheQ* tmp = refetchinst;
						refetchinst = refetchinst->next;
						delete tmp;

					}else{
						if(read_instruction(&inst)){

							addto_RS_cpr(inst,PC);
							addto_Record();

							recordtail->fetch = (recordtail->inst_num - exceptbarri-1)/F + exceptcycle + 1;
							recordtail->disp = recordtail->fetch + 1;
							recordtail->sched = cycle + 1;   

							DispQ_length--;
							dsize--;
							PC++;

						}else{
							DispQ_length = -1;
							non_read = true;
						}
					}

				}else{
					break;
				}
			}

			//------------------------delete in RS-------------------------------
			for(int i=0;i<R;i++){
				if(retire[i]==NULL){
					break;
				}

				ScheQ* rs2 = RS_head;
				rs1 = NULL;
				while(rs2!=NULL){
					if(rs2==retire[i]){
						if(rs1 == NULL){
							RS_head = rs2->next;
							delete rs2;
							freeRS++;
							break;
						}else{
							rs1->next = rs2->next;
							delete rs2;
							freeRS++;
							break;
						}
					}
					rs1=rs2;
					rs2=rs2->next;		
				}

			}

			//------------------------fetch--------------------------------------
			
			if(DispQ_length>=0){
				DispQ_length += F;
			}
			
			if(fetch_pc+F <= 100000){
				fetch_pc += F;
				dsize += F;
			}else{
				if(fetch_pc < 100000){
					dsize = dsize + 100000-fetch_pc;
					fetch_pc = 100000;
				}
			}

			p_stats-> avg_disp_size += old_dsize;
			if(old_dsize > max_size){
				max_size = old_dsize;
			}

			
			
			if((DispQ_length<0)&&(refetchinst==NULL)&&(RS_head==NULL)&&(CPQ_head==NULL)&&(robhead==NULL)){
				cycle=-10;
			}


		}
	}
	p_stats->reg_file_hit_count = reghits;
	p_stats->rob_hit_count = robhits;
	p_stats->backup_count = backups;
	p_stats->flushed_count = flushes;
	p_stats->avg_inst_retired = (float)(p_stats->retired_instruction + 0.0)/p_stats->cycle_count;
	p_stats->avg_inst_fired = (float)(fireds + 0.0)/p_stats->cycle_count;
	p_stats-> avg_disp_size = (float) (p_stats-> avg_disp_size)/(p_stats->cycle_count+0.0);
	p_stats->max_disp_size = max_size;

}

/**
 * Subroutine for cleaning up any outstanding instructions and calculating overall statistics
 * such as average IPC, average fire rate etc.
 * XXX: You're responsible for completing this routine
 *
 * @p_stats Pointer to the statistics structure
 */
void complete_proc(proc_stats_t *p_stats) 
{
	printf("\n");
}
