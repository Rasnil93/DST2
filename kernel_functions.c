#include "kernel_functions.h"

int Ticks; /* global sysTick counter */
int KernelMode; /* can be equal to either INIT or RUNNING (constants defined * in �kernel_functions.h�)*/
TCB *PreviousTask, *NextTask; /* Pointers to previous and next running tasks */
list *ReadyList;
list *WaitingList;
list *TimerList;
uint TC;


void idle();

//listobj *extractWaitingList(listobj *package);
//void insertIntoWaitingList(mailbox *mBox,TCB *tcb);

listobj *extract(list *theList);
void insertIntoList(list *theList, listobj *newListObj);

msg *dequeueFromMailbox(mailbox *mBox);
exception receive_no_wait(mailbox* mBox, void *pData);

listobj *extractWaitingAndTimerList(list *theList, listobj *theObj);


void enqueueIntoMailbox(mailbox* mBox, msg *message){ //enqueue into head
  if(mBox->pHead == NULL && mBox->pTail == NULL){ // is empty
      mBox->pHead = message;
      mBox->pTail = message;
      mBox->nMessages += SENDER;
    }else{ 
      if(mBox->pHead == mBox->pTail){
        mBox->pHead = message;
        message->pNext = mBox->pTail;
        mBox->pTail->pPrevious = message;
      }else{
        mBox->pHead->pPrevious = message;
        message->pNext = mBox->pHead;
        mBox->pHead = message;     
      }
      mBox->nMessages += SENDER;
  }
}

msg *dequeueFromMailbox(mailbox *mBox){ //take msg from tail
   isr_off();
    if(mBox->pHead == NULL && mBox->pTail == NULL){ // is empty
        isr_on();
        return NULL;
    }else if (mBox->pHead == mBox->pTail){ //size == 1
        msg *tempMsg = mBox->pHead;
        free(mBox->pHead);
        free(mBox->pTail);
        mBox->pHead = NULL;
        mBox->pTail = NULL;
        mBox->nMessages += RECEIVER;
        isr_on();
        return tempMsg;
    }else{ //more than one item
        msg *tempMsg = mBox->pTail;
        
        mBox->pTail->pPrevious->pNext = NULL;
        free(mBox->pTail);
        mBox->pTail = tempMsg->pPrevious;
        mBox->nMessages += RECEIVER;
        
        isr_on();
        return tempMsg;
    }
}

msg *removeHeaderMsgFromMailbox(mailbox *mBox){
   isr_off();
    if(mBox->pHead == NULL && mBox->pTail == NULL){ // is empty
        isr_on();
        return NULL;
    }else if (mBox->pHead == mBox->pTail){ //size == 1
        msg *tempMsg = mBox->pHead;
        free(mBox->pHead);
        free(mBox->pTail);
        mBox->pHead = NULL;
        mBox->pTail = NULL;
        mBox->nMessages += RECEIVER;
        isr_on();
        return tempMsg;
    }else{ //more than one item
        msg *tempMsg = mBox->pHead;
        
        mBox->pHead->pNext->pPrevious = NULL;
        free(mBox->pHead);
        mBox->pHead = tempMsg->pNext;
        mBox->nMessages += RECEIVER;
        isr_on();
        return tempMsg;
    }
}

mailbox* create_mailbox(uint nMessages, uint nDataSize){
  isr_off();
  mailbox *theMailBox = (mailbox *) calloc(1, sizeof(mailbox));
  
  theMailBox->nMaxMessages = nMessages;
  theMailBox->nDataSize = nDataSize;
  theMailBox->nMessages = 0;
  theMailBox->nBlockedMsg = 0;
  isr_on();
  
  return theMailBox;
}

exception remove_mailbox(mailbox *mBox){
  if(mBox->nMessages == 0){
    free(mBox);
    return OK;
  }else{
    return NOT_EMPTY;
  }
}


exception send_wait(mailbox *mBox, void *pData){
  isr_off();
  msg *tempMsg;
  
  if(mBox->nMessages < 0){
    tempMsg = mBox->pHead;//take the last inserted msg
    memcpy(mBox->pTail->pData, pData,mBox->nDataSize); //copy sendars data to the data area of receivers msg
    
    //remove recieving task's message from the mailbox
    tempMsg = tempMsg->pNext;
    tempMsg->pPrevious = NULL;
    free(mBox->pHead);
    mBox->pHead = tempMsg;

    //update previous/next task
    PreviousTask = NextTask;
    listobj *extractedObj = extractWaitingAndTimerList(WaitingList, mBox->pHead->pBlock)->pMessage->pBlock = mBox->pHead->pBlock;
    insertIntoList(ReadyList, extractedObj);//insert into readylist, should be a TCB but sending void?
    NextTask = ReadyList->pHead->pTask;
  }else{
    tempMsg = (msg *) calloc(1, sizeof(msg)); //allocate a msg datapointer
    
    //add message to the mailbox should be fifo
    tempMsg->pBlock = ReadyList->pHead;
    tempMsg->pData = pData;
    
    //enqueue into mailbox
    enqueueIntoMailbox(mBox, tempMsg);
    
    //update previous/nexttask move task from readylist to waitinglist
    PreviousTask = NextTask;
    listobj* extractedNode = extract(ReadyList);
    extractedNode->pThisTasksMailbox = mBox;
    extractedNode->pMessage = tempMsg;
    insertIntoList(WaitingList, extractedNode);
    NextTask = ReadyList->pHead->pTask;
  }
    
  SwitchContext();
  
  if(ticks() >= deadline()){ //if deadling is reached
    //remove head msg from the mailbox
    removeHeaderMsgFromMailbox(mBox);
    return DEADLINE_REACHED;
  }else{
    return OK;
  }
}



exception receive_wait(mailbox * mBox, void * pData){
  
  msg *message;
  isr_off();
  if(mBox->nMessages > 0){
    //remove sendig task's message from the mailbox
    message = dequeueFromMailbox(mBox);

    //copy senders data to receiving task's data area
    memcpy(pData, message->pData, mBox->nDataSize);
    
    if(message->pBlock != NULL){
      PreviousTask = NextTask;
      listobj* extractedNode = extractWaitingAndTimerList(WaitingList, message->pBlock);
      //memcpy(message, extractedNode->pMessage, sizeof(message));
      //extractedNode->pMessage = message;
      insertIntoList(ReadyList, extractedNode);
      NextTask = ReadyList->pHead->pTask;
    }else{
      free(message);
    }
  }else{
    //allocate place for msg
    message = (msg *) calloc(1, sizeof(msg));
    message->pData = pData;
    message->pBlock = ReadyList->pHead;
    
    //add message to the mailbox, using enqueue and insert into header
    enqueueIntoMailbox(mBox, message);
    
    PreviousTask = NextTask;
    listobj* extractedNode = extract(ReadyList);
    extractedNode->pMessage = message;
      
    insertIntoList(WaitingList,extractedNode);
    NextTask = ReadyList->pHead->pTask;
  }
  SwitchContext();
  isr_on();
  
  if(ticks() > deadline()){
    //remove head msg from the mailbox
    removeHeaderMsgFromMailbox(mBox);
    
    return DEADLINE_REACHED;
  }else{
    return OK;
  }
}
   
exception send_no_wait(mailbox* mBox, void *pData){
   isr_off();
 msg* message;
   if(mBox->nMessages < 0) { 
     
      message = mBox->pHead;//take the last inserted msg
      message->pData = malloc(sizeof(pData));
      memcpy(message->pData, pData , sizeof(pData));
      
      
     // memcpy(message->pData, pData,mBox->nDataSize); //copy sendars data to the data area of receivers msg
      
      //remove recieving task's message from the mailbox
      msg* tempMsg = removeHeaderMsgFromMailbox(mBox);
      
      PreviousTask = NextTask;
      insertIntoList(ReadyList, extractWaitingAndTimerList(WaitingList, tempMsg->pBlock));
      NextTask = ReadyList->pHead->pTask;
      SwitchContext();
   }else{
      message =  (msg *) calloc(1, sizeof(msg));
      message->pData = malloc(sizeof(pData));
      //message->pData = (void*)calloc(1,sizeof(mBox->nDataSize));
      memcpy(message->pData, pData , sizeof(pData));

      //message->pData = pData;
      message->pBlock = NULL;
      
      //memcpy(message->pData, pData, mBox->nDataSize);
      
      if(mBox->nMessages == mBox->nMaxMessages){ // full mailbox
        dequeueFromMailbox(mBox);
      }
      
     enqueueIntoMailbox(mBox, message);
   }
   return OK;
}

exception receive_no_wait(mailbox* mBox, void *pData){
  isr_off();
  msg* message;
  if(mBox->nMessages > 0) { 
    //remove sendig task's message from the mailbox
    message = dequeueFromMailbox(mBox);

    //copy senders data to receiving task's data area
    memcpy(pData, message->pData, mBox->nDataSize);
    
    if(message->pBlock != NULL){
      PreviousTask = NextTask;
      insertIntoList(ReadyList, extractWaitingAndTimerList(WaitingList, message->pBlock));
      NextTask = ReadyList->pHead->pTask;
      SwitchContext();
    }else{
      
      //mBox->pTail = mBox->pTail->pPrevious;
      //free(mBox->pTail->pNext);
      //mBox->pTail->pNext = NULL;
    }
   
  }else{
    return FAIL;
  }
  return OK;
}


exception init_kernel(void){
  
  Ticks = 0;
  TC = 0;
  
  //isr_off();
      ReadyList = (list *) calloc(1, sizeof(list));
      if(ReadyList == NULL){
        return FAIL;
      }
       WaitingList = (list *) calloc(1, sizeof(list));
      if(WaitingList == NULL){
        return FAIL;
      }
      TimerList = (list *) calloc(1, sizeof(list));
      if(TimerList == NULL){
        return FAIL;
      }
  //isr_on();
  
  create_task(idle, UINT_MAX);

    KernelMode = INIT;

  return OK;
}


void run (void){
  Ticks = 0;

  KernelMode = RUNNING;

  NextTask = ReadyList->pHead->pTask;

  LoadContext_In_Run();
}

listobj *extract(list *theList){
  //isr_off();
    if(theList->pHead == NULL && theList->pTail == NULL){ // is empty
      //isr_on();
      return NULL;
    }else if (theList->pHead == theList->pTail){ //size == 1
        listobj *tempObj = theList->pHead;
        theList->pHead = NULL;
        theList->pTail = NULL;
        //isr_on();
        return tempObj;
    }else{
        listobj *tempObj = theList->pHead;
        if(tempObj->pNext == theList->pTail){
          theList->pHead = theList->pTail;
          theList->pHead->pNext = NULL;
          theList->pTail->pPrevious = NULL;
        }else{          
          theList->pHead = theList->pHead->pNext;
          theList->pHead->pPrevious = NULL;
        }
        //isr_on();
        return tempObj;
    }

}

listobj *extractWaitingAndTimerList(list *theList, listobj *theObj){
 // isr_off();
    if(theList->pHead == NULL && theList->pTail == NULL){ // is empty
      return NULL;
    }else if (theList->pHead == theList->pTail){ //size == 1
        if(theObj->pTask == theList->pHead->pTask){
          listobj *tempObj = theList->pHead;
          theList->pHead = NULL;
          theList->pTail = NULL;
          return tempObj;
        }else{
          return NULL;
        }
        //isr_on();
    }else{
        listobj *tempObj = theList->pHead;
        listobj *theTaskObj;

        while (tempObj->pNext != NULL){
            if(tempObj->pTask == theObj->pTask){
               if(tempObj == theList->pHead){ //pop head
                 theTaskObj = theList->pHead;
                 theList->pHead = theList->pHead->pNext;
                 theList->pHead->pPrevious = NULL;
          //       isr_on();
                 return theTaskObj;
               }else{
                 if(tempObj->pTask != theList->pTail->pTask){ //pop in the middle
                    theTaskObj = tempObj;
                    tempObj->pPrevious->pNext = tempObj->pNext;
                    tempObj->pNext->pPrevious = tempObj->pPrevious;
                   // isr_on();
                    return theTaskObj;
                 }
               }
            }
            
         
              tempObj = tempObj->pNext;
            
        }
        
        if(tempObj->pTask == theList->pTail->pTask){ //pop tail
          theTaskObj = theList->pTail;
          theList->pTail = theList->pTail->pPrevious;
          theList->pTail->pNext = NULL;
          //isr_on();
          return theTaskObj;
        }
    }

}


void insertIntoList(list *theList, listobj *newListObj){
 //isr_off();
    TCB *tcb = newListObj->pTask;
    newListObj->pNext = NULL;
    newListObj->pPrevious = NULL;
    if(theList->pHead == NULL && theList->pTail == NULL){ // is empty
        theList->pHead = newListObj;
        theList->pTail = newListObj;
    }else if (theList->pHead == theList->pTail){ //size == 1
        if(theList->pHead->pTask->Deadline > tcb->Deadline){//our new TCB has lower deadline
            newListObj->pNext = theList->pTail;
            theList->pHead = newListObj;
            theList->pTail->pPrevious = newListObj;
        }else{//our new TCB has higher deadline
            newListObj->pPrevious = theList->pHead;
            theList->pTail = newListObj;
            theList->pHead->pNext = newListObj;
        }
    }else{ //multiple items in list larger than 3 items in list
        listobj *tempObj = theList->pHead;

        while (tempObj->pNext != NULL){
            if(tempObj->pTask->Deadline > tcb->Deadline){
                if(tempObj == theList->pHead){
                  newListObj->pNext = theList->pHead;
                  theList->pHead->pPrevious = newListObj;
                  theList->pHead = newListObj;

                  //newListObj->pPrevious = tempObj->pPrevious;

                  //tempObj->pPrevious = newListObj;
                }else{
                  tempObj->pPrevious->pNext = newListObj;

                  newListObj->pPrevious = tempObj->pPrevious;
                  newListObj->pNext = tempObj;

                  tempObj->pPrevious = newListObj;
                }
                break;
            }
            tempObj = tempObj->pNext;
        }

            if(tempObj == theList->pTail){ //new deadline is the largest put in back
              if(tempObj->pTask->Deadline > tcb->Deadline){
                newListObj->pPrevious = theList->pTail->pPrevious;
                newListObj->pNext = theList->pTail;
                theList->pTail->pPrevious->pNext = newListObj;
                theList->pTail->pPrevious = newListObj;
              }else{
                theList->pTail->pNext = newListObj;
                newListObj->pPrevious = theList->pTail;
                theList->pTail = newListObj;
              }
                /*newListObj->pPrevious = theList->pTail->pPrevious;
                theList->pTail->pPrevious->pNext = newListObj;
                theList->pTail->pPrevious = newListObj;
                newListObj->pNext = theList->pTail;*/
            }
    }
    
   //     isr_on();
}


exception create_task(void(*task_body)(), uint deadline){
    TCB *newTcb;
    newTcb = (TCB *) calloc(1, sizeof(TCB));
    
    listobj *newListObj = (listobj *) calloc(1, sizeof(listobj));
    
    if(newTcb == NULL){
      return FAIL;
    }
    //check if callco returns ok eller n�got

    newTcb->PC = task_body;
    newTcb->SPSR = 0x21000000;
    newTcb->Deadline = deadline;

    newTcb->StackSeg[STACK_SIZE - 2] = 0x21000000;
    newTcb->StackSeg[STACK_SIZE - 3] = (unsigned int) task_body;
    newTcb->SP = &(newTcb->StackSeg[STACK_SIZE-9]);
    newListObj->pTask = newTcb;
    
    if(KernelMode == INIT){
        insertIntoList(ReadyList, newListObj);
        return OK;
    }else{
        isr_off();
        PreviousTask = NextTask;
        insertIntoList(ReadyList, newListObj);
          NextTask = ReadyList->pHead->pTask;
        SwitchContext();
        return OK;
    }
}


void terminate(void){
  isr_off();
  listobj *leavingObj = extract(ReadyList);
  NextTask = ReadyList->pHead->pTask;
  switch_to_stack_of_next_task();
  
  free(leavingObj->pTask);
  free(leavingObj);
  LoadContext_In_Terminate();
}


void idle(){
  while(1){}
}

/*
#############################################################
########################## LAB 3 ############################
#############################################################
*/

exception wait(uint nTicks){
  isr_off();
  PreviousTask = NextTask;
  ReadyList->pHead->nTCnt = nTicks+ticks();
  insertIntoList(TimerList,extract(ReadyList));
  NextTask = ReadyList->pHead->pTask;
  SwitchContext();
  //isr_on();
  if(ticks() > deadline()){
    return DEADLINE_REACHED;
  }else{
    return OK;
  }
}

void set_ticks(uint nTicks){
  TC = nTicks;
}


uint ticks(void){
  return TC;
}

uint deadline(void){
  return ReadyList->pHead->pTask->Deadline;
}

void set_deadline(uint deadline){
  isr_off();
  listobj *tempListObj = ReadyList->pHead;
  
  ReadyList->pHead->pTask->Deadline = deadline;
  PreviousTask = NextTask;
  
  if(ReadyList->pHead != ReadyList->pTail){//det �r fler �n 1 nod i listan
    if(tempListObj->pNext->pTask->Deadline < deadline){ // ifall n�sta nod har mindre deadline �n head �ndra f�rfan
       insertIntoList(ReadyList, extract(ReadyList));
    }
  }
  
  NextTask = ReadyList->pHead->pTask;
  SwitchContext();
  //isr_on();
}

void removeMsgFromMailbox(listobj *listObj){
  mailbox *mBox = listObj->pThisTasksMailbox;
  if (mBox->pHead == mBox->pTail){ //size == 1
    free(mBox->pHead);
    free(mBox->pTail);
    mBox->pHead = NULL;
    mBox->pTail = NULL;
    mBox->nMessages += RECEIVER;
  }
}

void TimerInt() {
    TC++;
    
    int isDoneTimer = 0;
    listobj *tempListObjTimerList = TimerList->pHead;
    while(isDoneTimer == 0 || tempListObjTimerList != NULL){
      if((tempListObjTimerList != NULL) &&(TC >= tempListObjTimerList->nTCnt || TC > tempListObjTimerList->pTask->Deadline)){
        PreviousTask = NextTask;
        insertIntoList(ReadyList, extractWaitingAndTimerList(TimerList, tempListObjTimerList));
        NextTask = ReadyList->pHead->pTask;
        break;
      }else{
        isDoneTimer = 1;
        break;
      }
      
         
      if(tempListObjTimerList == TimerList->pTail){
        isDoneTimer = 1;
        break;
      }else{
        tempListObjTimerList = tempListObjTimerList->pNext;
      }
    }
    
    
    listobj *tempListObjTimerInt = WaitingList->pHead;

    int isDone = 0;
    while(isDone == 0 || tempListObjTimerInt->pTask->Deadline != NULL){
      if(TC >= tempListObjTimerInt->pTask->Deadline){
        removeMsgFromMailbox(tempListObjTimerInt);
        PreviousTask = NextTask;
        insertIntoList(ReadyList, extractWaitingAndTimerList(WaitingList, tempListObjTimerInt->pMessage->pBlock));
        NextTask = ReadyList->pHead->pTask;
        break;
      }else{
        isDone = 1;
        break;
      }
      
      if(tempListObjTimerInt == WaitingList->pTail){
        isDone = 1;
        break;
      }else{
        tempListObjTimerInt = tempListObjTimerInt->pNext;
      }
    }
    
    
    

}