//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

pthread_mutex_t head_locked;

void init_head_lock () {
  pthread_mutex_init(&head_locked, 0);
}

void head_lock() {
  pthread_mutex_lock(&head_locked);
}

void head_unlock() {
  pthread_mutex_unlock(&head_locked);
}

static int degree_of_multiprogramming;
static int max_number_of_ram_frames_proc;
static struct proc_ram_frm_node * num_ram_frames_list = NULL;
pthread_mutex_t degree_lock;

void init_degree_of_multiprogramming() {
  pthread_mutex_init(&degree_lock, 0);
  degree_of_multiprogramming = 0;
}

void enlist_num_ram_frames(struct pcb_t * caller, int numframe) {
  //TODO: change to find and plus if not then enlist
  if(num_ram_frames_list == NULL) {
    num_ram_frames_list = malloc(sizeof(struct proc_ram_frm_node));
    num_ram_frames_list->pcb = caller;
    num_ram_frames_list->num_ram_frames = numframe;
    num_ram_frames_list->next = NULL;
  }
  else {
    struct proc_ram_frm_node * p = malloc(sizeof(struct proc_ram_frm_node));
    p->pcb = caller;
    p->num_ram_frames = numframe;
    p->next = num_ram_frames_list;
    num_ram_frames_list = p;
  }
}

void update_num_ram_frames (struct pcb_t * caller, int numframe) {
  pthread_mutex_lock(&degree_lock);
  struct proc_ram_frm_node * pn = num_ram_frames_list;
  while(pn != NULL) {
    if(pn->pcb == caller) {
      pn->num_ram_frames += numframe;
      pthread_mutex_unlock(&degree_lock);
      return;
    }
    pn = pn->next;
  }
  pthread_mutex_unlock(&degree_lock);
}

int remain_num_ram_frame (struct pcb_t * caller) {
  pthread_mutex_lock(&degree_lock);
  
  struct proc_ram_frm_node * pn = num_ram_frames_list;
  while(pn != NULL) {
    if(pn->pcb == caller) {
      pthread_mutex_unlock(&degree_lock);
      return max_number_of_ram_frames_proc - pn->num_ram_frames;
    }
    pn = pn->next;
  }
  pthread_mutex_unlock(&degree_lock);
  return -10;
}

void detach_ram_frame_node(struct pcb_t * caller) {
  //TODO
  struct proc_ram_frm_node * pn = num_ram_frames_list;
  if(pn->next == NULL && pn->pcb == caller) {
    num_ram_frames_list = NULL;
    free(pn);

    return;
  }
  while(pn->next != NULL) {
    if(pn->next->pcb == caller) {
      struct proc_ram_frm_node * p = pn->next;
      pn->next = p->next;
      free(p);
    }
    pn = pn->next;

    return;
  }
  return;
}

void check_exceed_page () {
  head_lock();
  pthread_mutex_lock(&degree_lock);
  struct proc_ram_frm_node * p = num_ram_frames_list;
  while(p != NULL) {
    if(p->num_ram_frames > max_number_of_ram_frames_proc) {
      int num_need_swap = p->num_ram_frames - max_number_of_ram_frames_proc;
      for(int i = 0; i < num_need_swap; i++) {
        int swpfpn, vicpgn;
        if(MEMPHY_get_freefp(p->pcb->active_mswp, &swpfpn) == 0) {
          find_victim_page(p->pcb->mm, &vicpgn);
          int vicfpn = GETVAL(p->pcb->mm->pgd[vicpgn], PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
          MEMPHY_swap(p->pcb->mram, vicfpn, p->pcb->active_mswp ,swpfpn);
          pte_set_swap(&p->pcb->mm->pgd[vicpgn], 0, swpfpn);
          MEMPHY_put_freefp(p->pcb->mram, vicfpn);
          p->num_ram_frames--;
        }
      }
    }

    p = p->next;
  }
  pthread_mutex_unlock(&degree_lock);
  head_unlock();

  return;
}

int increase_degree_of_multiprogramming(struct pcb_t * caller) {
  pthread_mutex_lock(&degree_lock);
  degree_of_multiprogramming ++;
  max_number_of_ram_frames_proc = DIV_ROUND_UP(caller->mram->maxsz, PAGING_PAGESZ) / degree_of_multiprogramming;
  enlist_num_ram_frames(caller, 0);
  //TODO: check and page out pages of the process that exceed
  struct proc_ram_frm_node * p = num_ram_frames_list;
  while(p != NULL) {
    if(p->num_ram_frames > max_number_of_ram_frames_proc) {
      int num_need_swap = p->num_ram_frames - max_number_of_ram_frames_proc;
      for(int i = 0; i < num_need_swap; i++) {
        int swpfpn, vicpgn;
        if(MEMPHY_get_freefp(caller->active_mswp, &swpfpn) == 0) {
          find_victim_page(p->pcb->mm, &vicpgn);
          int vicfpn = GETVAL(p->pcb->mm->pgd[vicpgn], PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
          MEMPHY_swap(p->pcb->mram, vicfpn, caller->active_mswp ,swpfpn);
          pte_set_swap(&p->pcb->mm->pgd[vicpgn], 0, swpfpn);
          MEMPHY_put_freefp(p->pcb->mram, vicfpn);
          p->num_ram_frames--;
        }
      }
    }

    p = p->next;
  }
  pthread_mutex_unlock(&degree_lock);

  return 0;
}

int decrease_degree_of_multiprogramming(struct pcb_t * caller) {
  pthread_mutex_lock(&degree_lock);
  degree_of_multiprogramming --;
  if(degree_of_multiprogramming == 0) {
    max_number_of_ram_frames_proc = DIV_ROUND_UP(caller->mram->maxsz, PAGING_PAGESZ);
  }
  else {
    max_number_of_ram_frames_proc = DIV_ROUND_UP(caller->mram->maxsz, PAGING_PAGESZ) / degree_of_multiprogramming;
  }
  detach_ram_frame_node(caller);
  pthread_mutex_unlock(&degree_lock);

  return 0;
}

/* 
 * init_pte - Initialize PTE entry
 */
int init_pte(uint32_t *pte,
             int pre,    // present
             int fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             int swpoff) //swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      //if (fpn == 0) 
        //return -1; // Invalid setting

      /* Valid setting with FPN */
      *pte = 0;
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT); 
    } else { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT); 
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;   
}

/* 
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(uint32_t *pte, int swptyp, int swpoff)
{
  *pte = 0;
  CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/* 
 * pte_set_swap - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(uint32_t *pte, int fpn)
{
  *pte = 0;
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT); 

  return 0;
}


/* 
 * vmap_page_range - map a range of page at aligned address
 */
int vmap_page_range(struct pcb_t *caller, // process call
                                int addr, // start address which is aligned to pagesz
                               int pgnum, // num of mapping page
           struct framephy_struct *frames,// list of the mapped frames
              struct vm_rg_struct *ret_rg)// return mapped region, the real mapped fp
{                                         // no guarantee all given pages are mapped
  //uint32_t * pte = malloc(sizeof(uint32_t));
  //print_list_fp(frames);
  //int  fpn;
  int pgit = 0;
  int pgn = PAGING_PGN(addr);

  ret_rg->rg_end = ret_rg->rg_start = addr; // at least the very first space is usable


  /* TODO map range of frame to address space 
   *      [addr to addr + pgnum*PAGING_PAGESZ
   *      in page table caller->mm->pgd[]
   */
  struct framephy_struct * p = frames;
  uint32_t * table = caller->mm->pgd;
  for(pgit = 0; pgit < pgnum; pgit++) {
    if(init_pte(&table[pgn+pgit], 1, p->fpn, 0, 0, 0, 0) == 0) {
      struct framephy_struct * clr = p;
      p = p->fp_next;
      free(clr);
    }

   /* Tracking for later page replacement activities (if needed)
    * Enqueue new usage page */
    enlist_fifo_pgn_node(caller, pgn+pgit);
  }

  return 0;
}

/* 
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

int alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct** frm_lst)
{
  if(req_pgnum > max_number_of_ram_frames_proc) {
    return -3000;
  }
  
  int pgit, fpn;
  int swpfpn;
  struct framephy_struct *newfp_str;

  for(pgit = 0; pgit < req_pgnum; pgit++)
  {
    if(remain_num_ram_frame(caller) > 0 && MEMPHY_get_freefp(caller->mram, &fpn) == 0)
    {
      if(*frm_lst == NULL) {
        *frm_lst = malloc(sizeof(struct framephy_struct));
        (*frm_lst)->fpn = fpn;
        (*frm_lst)->fp_next = NULL;
        newfp_str = *frm_lst;
      }
      else {
        newfp_str->fp_next = malloc(sizeof(struct framephy_struct));
        newfp_str = newfp_str->fp_next;
        newfp_str->fpn = fpn;
        newfp_str->fp_next = NULL;
      }
      update_num_ram_frames(caller, 1);
    } 
    else if (MEMPHY_get_freefp(caller->active_mswp, &swpfpn) == 0){  // ERROR CODE of obtaining somes but not enough frames
      int vicpgn;
      find_victim_page(caller->mm, &vicpgn);
      int vicfpn = GETVAL(caller->mm->pgd[vicpgn], PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
      MEMPHY_swap(caller->mram, vicfpn, caller->active_mswp ,swpfpn);
      pte_set_swap(&caller->mm->pgd[vicpgn], 0, swpfpn);
      if(*frm_lst == NULL) {
        *frm_lst = malloc(sizeof(struct framephy_struct));
        (*frm_lst)->fpn = vicfpn;
        (*frm_lst)->fp_next = NULL;
        newfp_str = *frm_lst;
      }
      else {
        newfp_str->fp_next = malloc(sizeof(struct framephy_struct));
        newfp_str = newfp_str->fp_next;
        newfp_str->fpn = vicfpn;
        newfp_str->fp_next = NULL;
      }
    }
    else {
      return -3000;
    } 
 }
  //print_list_fp()

  return 0;
}


/* 
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
int vm_map_ram(struct pcb_t *caller, int astart, int aend, int mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  int ret_alloc;

  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide 
   *duplicate control mechanism, keep it simple
   */
  ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);

  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;

  /* Out of memory */
  if (ret_alloc == -3000) 
  {
#ifdef MMDBG
     printf("OOM: vm_map_ram out of memory \n");
#endif
     return -1;
  }

  /* it leaves the case of memory is enough but half in ram, half in swap
   * do the swaping all to swapper to get the all in ram */
  vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

  return 0;
}

/* Swap copy content page from source frame to destination frame 
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, int srcfpn,
                struct memphy_struct *mpdst, int dstfpn) 
{
  int cellidx;
  int addrsrc,addrdst;
  BYTE data;
  for(cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING_PAGESZ + cellidx;

    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }


  return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct * vma = malloc(sizeof(struct vm_area_struct));

  mm->pgd = malloc(PAGING_MAX_PGN*sizeof(uint32_t));

  /* By default the owner comes with at least one vma */
  vma->vm_id = 1;
  vma->vm_start = 0;
  vma->vm_end = vma->vm_start;
  vma->sbrk = vma->vm_start;
  struct vm_rg_struct *first_rg = init_vm_rg(vma->vm_start, vma->vm_end);
  enlist_vm_rg_node(&vma->vm_freerg_list, first_rg);

  vma->vm_next = NULL;
  vma->vm_mm = mm; /*point back to vma owner */

  mm->mmap = vma;

  mm->fifo_pgn = NULL;

  return 0;
}

struct vm_rg_struct* init_vm_rg(int rg_start, int rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct* rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, int pgn)
{
  if(*plist == NULL) {
    (*plist) = malloc(sizeof(struct pgn_t));
    (*plist)->pgn = pgn;
    (*plist)->pg_next = NULL;
    printf("enlist head %d\n", (*plist)->pgn);
    printf("%p\n", *plist);
    return 0;
  }
  else {
    struct pgn_t * p = *plist;
    while(p->pg_next != NULL) p = p->pg_next;
    p->pg_next = malloc(sizeof(struct pgn_t));
    p->pg_next->pgn = pgn;
    p->pg_next->pg_next = NULL;
    printf("enlist %d\n", p->pg_next->pgn);
    printf("%p\n", *plist);
  }

  return 0;
}

int enlist_fifo_pgn_node(struct pcb_t * caller, int pgn)
{
  if(caller->mm->fifo_pgn == NULL) {
    caller->mm->fifo_pgn = malloc(sizeof(struct pgn_t));
    caller->mm->fifo_pgn->pgn = pgn;
    caller->mm->fifo_pgn->pg_next = NULL;
    return 0;
  }
  else {
    struct pgn_t * p = caller->mm->fifo_pgn;
    while(p->pg_next != NULL) p = p->pg_next;
    p->pg_next = malloc(sizeof(struct pgn_t));
    p = p->pg_next;
    p->pgn = pgn;
    p->pg_next = NULL;
    p = NULL;
  }

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
   struct framephy_struct *fp = ifp;
 
   printf("print_list_fp: ");
   if (fp == NULL) {printf("NULL list\n"); return -1;}
   printf("\n");
   while (fp != NULL )
   {
       printf("fp[%d]\n",fp->fpn);
       fp = fp->fp_next;
   }
   printf("\n");
   return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
   struct vm_rg_struct *rg = irg;
 
   printf("print_list_rg: ");
   if (rg == NULL) {printf("NULL list\n"); return -1;}
   printf("\n");
   while (rg != NULL)
   {
       printf("rg[%ld->%ld]\n",rg->rg_start, rg->rg_end);
       rg = rg->rg_next;
   }
   printf("\n");
   return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
   struct vm_area_struct *vma = ivma;
 
   printf("print_list_vma: ");
   if (vma == NULL) {printf("NULL list\n"); return -1;}
   printf("\n");
   while (vma != NULL )
   {
       printf("va[%ld->%ld]\n",vma->vm_start, vma->vm_end);
       vma = vma->vm_next;
   }
   printf("\n");
   return 0;
}

int print_list_pgn(struct pcb_t * caller ,struct pgn_t *ip)
{
   printf("print_list_pgn of process %d: ", caller->pid);
   if (ip == NULL) {printf("NULL list\n"); return -1;}
   printf("\n");
   while (ip != NULL )
   {
       printf("va[%d]-\n",ip->pgn);
       ip = ip->pg_next;
   }
   printf("\n");
   return 0;
}

int print_pgtbl(struct pcb_t *caller, uint32_t start, uint32_t end)
{
  int pgn_start,pgn_end;
  int pgit;

  if(end == -1){
    pgn_start = 0;
    struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, 0);
    end = cur_vma->vm_end;
  }
  pgn_start = PAGING_PGN(start);
  pgn_end = PAGING_PGN(end);

  printf("print_pgtbl of process %d: %d - %d", caller->pid, start, end);
  if (caller == NULL) {printf("NULL caller\n"); return -1;}
    printf("\n");


  for(pgit = pgn_start; pgit < pgn_end; pgit++)
  {
     printf("%08ld: %08x\n", pgit * sizeof(uint32_t), caller->mm->pgd[pgit]);
  }

  return 0;
}

//#endif
