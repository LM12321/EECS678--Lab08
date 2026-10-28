/** @file libscheduler.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libscheduler.h"
#include "../libpriqueue/libpriqueue.h"

#include <assert.h>   // for debugging
#include <stdbool.h> // for flags


/**
  Stores information making up a job to be scheduled including any statistics.

  You may need to define some global variables or a struct to store your job queue elements. 
*/
typedef struct _job_t
{
  int id;
  int arrival_time;
  int burst_time;
  int remaining_time;  // for premtive short short first
  int priority;

  int start_time;
  int end_time;

  int time_last_updated;
  int quantum_order;

} job_t;

// #define DEBUG



priqueue_t job_queue;
scheme_t job_scheme;
int total_cores;
job_t** core_status;



int total_jobs_created = 0;
int total_jobs_completed = 0;
priqueue_t completed_jobs;

int completed_jobs_comparer(const void* a, const void* b){
  return ((job_t*)a)->end_time - ((job_t*)b)->end_time;
}

// job_t** completed_jobs = NULL;


// make each job comparer depending on scheme

/*
  For a given priqueue_t<T> (elements of type T)...

  comparer: (l: T, r: T) -> int
    - given 2 elements of type T, returns an int representing the comparison status of the element priority values
    - comparer(l, r) < 0 <=> l is higher priority than r
    - comparer(l, r) > 0 <=> l is lower priority than r
    - comparer(l, r) == 0 <=> l is same priority as r
*/

// fcfs or first come first serve
int fcfs_comparer(const void* a, const void* b);

int fcfs_comparer(const void* a, const void* b){

  return ((job_t*)a)->arrival_time - ((job_t*)b)->arrival_time;

}

// sjf or shortest job first
int sjf_comparer(const void* a, const void* b){

  const int difference = ((job_t*)a)->burst_time - ((job_t*)b)->burst_time;
  if (difference == 0){
    return fcfs_comparer(a, b);
  }

  return difference;
}

int psjf_comparer(const void* a, const void* b){
  const int difference = ((job_t*)a)->remaining_time - ((job_t*)b)->remaining_time;
  if (difference == 0){
    return fcfs_comparer(a, b);
  }
  return difference;
}

int priority_comparer(const void* a, const void* b){
  const int difference = ((job_t*)a)->priority - ((job_t*)b)->priority;

  if (difference == 0){
    return fcfs_comparer(a,b);
  }

  return difference;
}

int rr_comparer(const void* a, const void* b){
  return ((job_t*)a)->quantum_order - ((job_t*)b)->quantum_order;
}

job_t* initialize_new_job(int job_number, int time, int running_time, int priority){
  
  job_t* new_job = (job_t*)calloc(1, sizeof(job_t));
  total_jobs_created++;

  if (new_job == NULL){
    fprintf(stderr, "ERROR: unable to create new job\n");
    exit(EXIT_FAILURE);
  }

  new_job->id = job_number;
  new_job->arrival_time = time;
  new_job->burst_time = running_time;
  new_job->remaining_time = running_time;  // for psjf
  new_job->priority = priority;
  new_job->start_time = -1;
  new_job->quantum_order = 0;

  return new_job;
}


void put_job_in_core(job_t* job, int time, int core_index){
  if (job->start_time == -1){
    job->start_time = time;
  }
  job->time_last_updated = time;
  core_status[core_index] = job;

}

void preempt_job_with_new_job(job_t* new_job, job_t* preempt_job, int time, int core_index){
  put_job_in_core(new_job, time, core_index);

  if (preempt_job->start_time == time){ // reset preempt job start time if preempt job hasn't done anything
    preempt_job->start_time = -1;
  }
  priqueue_offer(&job_queue, preempt_job);
}


/**
  Initalizes the scheduler.
 
  Assumptions:
    - You may assume this will be the first scheduler function called.
    - You may assume this function will be called once once.
    - You may assume that cores is a positive, non-zero number.
    - You may assume that scheme is a valid scheduling scheme.

  @param cores the number of cores that is available by the scheduler. These cores will be known as core(id=0), core(id=1), ..., core(id=cores-1).
  @param scheme  the scheduling scheme that should be used. This value will be one of the six enum values of scheme_t
*/

void scheduler_start_up(int cores, scheme_t scheme)
{
  if (cores <= 0){
    fprintf(stderr, "Invalid number of cores given: %d\n", cores);
    exit(EXIT_FAILURE);
  }

  total_cores = cores;
  job_scheme = scheme;
  core_status = (job_t**)calloc(cores, sizeof(job_t*));

  int (*comparer) (const void* a, const void* b);
  switch (job_scheme){
    case FCFS:
      comparer = fcfs_comparer;
      break;
    case SJF:
      comparer = sjf_comparer;
      break;
    case PSJF:
      comparer = psjf_comparer;
      break;
    case PRI: // priority and premptive priority can be the same when using the job_queue. Difference will be in scheduler new job functions
    case PPRI:
      comparer = priority_comparer;
      break;
    case RR:

      // figure out weirdo zone here seperately
      comparer = rr_comparer; // round robin is in first come first serve order
      break;
    default:
      fprintf(stderr, "ERROR: invalid job queue scheme given\n");
      exit(EXIT_FAILURE);
  }

  priqueue_init(&job_queue, comparer);
  
  // easiest way for me to think to implement this without making like a stack
  priqueue_init(&completed_jobs, completed_jobs_comparer);

}


/**
  Called when a new job arrives.
 
  If multiple cores are idle, the job should be assigned to the core with the
  lowest id.
  If the job arriving should be scheduled to run during the next
  time cycle, return the zero-based index of the core the job should be
  scheduled on. If another job is already running on the core specified,
  this will preempt the currently running job.
  Assumption:
    - You may assume that every job wil have a unique arrival time.

  @param job_number a globally unique identification number of the job arriving.
  @param time the current time of the simulator.
  @param running_time the total number of time units this job will run before it will be finished.
  @param priority the priority of the job. (The lower the value, the higher the priority.)
  @return index of core job should be scheduled on
  @return -1 if no scheduling changes should be made. 
 
 */
int scheduler_new_job(int job_number, int time, int running_time, int priority)
{

  job_t* new_job = initialize_new_job(job_number, time, running_time, priority); // initialize new job
  int i;


  // check if any cores are empty
  for (i = 0; i < total_cores; i++){

    if (core_status[i] == NULL){
      put_job_in_core(new_job, time, i);
      return i;
    }
  }

  switch (job_scheme)
  {
  case PSJF: {

    // initialize variables
    int best_core_index = -1;
    job_t* preempt_job = NULL;

    for (i = 0; i < total_cores; i++){
      assert(core_status[i] != NULL);
    
      job_t* compare_job = core_status[i]; 

      compare_job->remaining_time -= (time - compare_job->time_last_updated);   // update remaining time for all cores
      compare_job->time_last_updated = time;


      if (new_job->remaining_time < compare_job->remaining_time){
        if (best_core_index == -1 || psjf_comparer(compare_job, preempt_job) > 0){ 
          preempt_job = compare_job;
          best_core_index = i;
        }
          
      }
    }

    if (best_core_index != -1){
      preempt_job_with_new_job(new_job, preempt_job, time, best_core_index);
      return best_core_index;
    }
    
    break;
  }
  // might need to change to pick the most optimal core and not the first it sees
  case PPRI: {

    int best_core_index = -1;
    job_t* preempt_job = NULL;

    for (i = 0; i < total_cores; i++){
      assert(core_status[i] != NULL);

      job_t* compare_job = core_status[i];
      /**/
      if (new_job->priority < compare_job->priority){  // new job had a greater priority
        if (best_core_index == -1 || priority_comparer(compare_job, preempt_job) > 0){
          best_core_index = i;
          preempt_job = compare_job;
        }
          
      } 
    }
    if (best_core_index != -1){
      preempt_job_with_new_job(new_job, preempt_job, time, best_core_index);
      return best_core_index;
    } 

    break;
  }
  case RR: 
    //quantum order is already initialized to 0 so only alter it if things in job queue
    if (priqueue_peek(&job_queue) != NULL)
      new_job->quantum_order = priqueue_size(&job_queue);  // set the quantum order to 1 after the size of the job queue
  default:  // if we don't allow premting in the first place
    priqueue_offer(&job_queue, new_job);
    return -1;
    break;
  }
  
  // preempt failed
  priqueue_offer(&job_queue, new_job); // new job must wait for ready queue
  return -1;
}


/**
  Called when a job has completed execution.
 
  The core_id, job_number and time parameters are provided for convenience. You may be able to calculate the values with your own data structure.
  If any job should be scheduled to run on the core free'd up by the
  finished job, return the job_number of the job that should be scheduled to
  run on core core_id.
 
  @param core_id the zero-based index of the core where the job was located.
  @param job_number a globally unique identification number of the job.
  @param time the current time of the simulator.
  @return job_number of the job that should be scheduled to run on core core_id
  @return -1 if core should remain idle.
 */
int scheduler_job_finished(int core_id, int job_number, int time)
{
  job_t* finished_job = core_status[core_id]; 

  assert(finished_job != NULL); // ensure that finished_job is in fact not null
  assert(finished_job->id == job_number); // ensure that we are removing the correct job

  // update finished job
  finished_job->remaining_time = 0;
  finished_job->time_last_updated = time;
  finished_job->end_time = time; 

  core_status[core_id] = NULL;

  priqueue_offer(&completed_jobs, finished_job);  // add finished job to completed jobs "queue"
  total_jobs_completed++;
  
  if (priqueue_peek(&job_queue) == NULL){
    return -1;
  }

  job_t* job_to_add = priqueue_remove_at(&job_queue, 0);

  put_job_in_core(job_to_add, time, core_id);

  if (job_scheme == RR){
    // decrement all quantum orders in the job queue by 1 to account for the decrease in size
    for (int i = 0; i < priqueue_size(&job_queue); i++){
      job_t* job = priqueue_at(&job_queue, i);
      job->quantum_order -= 1;
      assert(job->quantum_order >=0);
    }
  }

  return job_to_add->id;

}


/**
  When the scheme is set to RR, called when the quantum timer has expired
  on a core.
 
  If any job should be scheduled to run on the core free'd up by
  the quantum expiration, return the job_number of the job that should be
  scheduled to run on core core_id.

  @param core_id the zero-based index of the core where the quantum has expired.
  @param time the current time of the simulator. 
  @return job_number of the job that should be scheduled on core cord_id
  @return -1 if core should remain idle
 */
int scheduler_quantum_expired(int core_id, int time)
{
  job_t* expired_job = core_status[core_id];  
  assert(expired_job != NULL);
  expired_job->quantum_order = priqueue_size(&job_queue); // set quantum order to current size of job queue
  if (priqueue_peek(&job_queue) == NULL){
    core_status[core_id] = expired_job; // just add the expired job back into job queue
    return expired_job->id;
  }
 
  priqueue_offer(&job_queue, expired_job); // add expired job to job queue
  // new job to add
  job_t* new_job = priqueue_remove_at(&job_queue, 0);
  new_job->quantum_order = -1; // set quantum order to a negative value

  put_job_in_core(new_job, time, core_id);

  // decrement all quantum orders in the job queue by 1 to account for the decrease in size
  for (int i = 0; i < priqueue_size(&job_queue); i++){
    job_t* job = priqueue_at(&job_queue, i);
    job->quantum_order -= 1;
    assert(job->quantum_order >=0);
  }
	return new_job->id;
}


/**
  Returns the average waiting time of all jobs scheduled by your scheduler.

  Assumptions:
    - This function will only be called after all scheduling is complete (all jobs that have arrived will have finished and no new jobs will arrive).
  @return the average waiting time of all jobs scheduled.
 */
float scheduler_average_waiting_time()
{
  if (priqueue_peek(&completed_jobs) == NULL){
    fprintf(stderr, "ERROR: schedule average waiting time, completed jobs queue was empty\n");
    exit(EXIT_FAILURE);
  }

  int total_waiting_time = 0;
  int n = priqueue_size(&completed_jobs);

  int i;

  for (i = 0; i < n; i++){
    job_t* job = priqueue_at(&completed_jobs, i);
    assert(job != NULL);  // job should not be empty when iterating through queue

    total_waiting_time += job->end_time - job->arrival_time - job->burst_time;
  }
  return (float)total_waiting_time/n;
  
}


/**
  Returns the average turnaround time of all jobs scheduled by your scheduler.

  Assumptions:
    - This function will only be called after all scheduling is complete (all jobs that have arrived will have finished and no new jobs will arrive).
  @return the average turnaround time of all jobs scheduled.
 */
float scheduler_average_turnaround_time()
{

  if (priqueue_peek(&completed_jobs) == NULL){
    fprintf(stderr, "ERROR: schedule average turnaround time, completed jobs queue was empty\n");
    exit(EXIT_FAILURE);
  }

  int total_turnaround_time = 0;
  int n = priqueue_size(&completed_jobs);
  for (int i = 0; i < n; i++){
    job_t* job = priqueue_at(&completed_jobs, i);
    total_turnaround_time += job->end_time - job->arrival_time;
  }
  return (float)total_turnaround_time/n;

}


/**
  Returns the average response time of all jobs scheduled by your scheduler.

  Assumptions:
    - This function will only be called after all scheduling is complete (all jobs that have arrived will have finished and no new jobs will arrive).
  @return the average response time of all jobs scheduled.
 */
float scheduler_average_response_time()
{

  if (priqueue_peek(&completed_jobs) == NULL){
    fprintf(stderr, "ERROR: schedule average response time, completed jobs queue was empty\n");
    exit(EXIT_FAILURE);
  }
  
  int total_response_time = 0;
  int n = priqueue_size(&completed_jobs);
  assert(total_jobs_created == total_jobs_completed);
  for (int i = 0; i < n; i++){
    job_t* job = priqueue_at(&completed_jobs, i);
    assert(job != NULL);
    
    if (job_scheme == PSJF){
      ;
      #ifdef DEBUG
        
        printf("Job %d: arrival=%d, first_start=%d, response=%d\n",
        job->id,
        job->arrival_time,
        job->start_time,
        job->start_time - job->arrival_time);
      #endif
    
    }
       
    total_response_time += job->start_time - job->arrival_time;
  }

  return (float)total_response_time/n;

}


/**
  Free any memory associated with your scheduler.
 
  Assumption:
    - This function will be the last function called in your library.
*/
void scheduler_clean_up()
{
  
  int i, n;
  n = priqueue_size(&job_queue);
  for (i = (n-1); i >= 0; i--){
    free(priqueue_remove_at(&job_queue, i));
  }
  priqueue_destroy(&job_queue);

  n = priqueue_size(&completed_jobs);
  for (i = (n-1); i >= 0; i--){
    free(priqueue_remove_at(&completed_jobs, i));
  } 
  priqueue_destroy(&completed_jobs);

  for (i = 0; i < total_cores; i++){
    free(core_status[i]);
  }

  free(core_status);


}


/**
  This function may print out any debugging information you choose. This
  function will be called by the simulator after every call the simulator
  makes to your scheduler.
  In our provided output, we have implemented this function to list the jobs in the order they are to be scheduled. 
  Furthermore, we have also listed the current state of the job (either running on a given core or idle). 
  For example, if we have a non-preemptive algorithm and job(id=4) has began running, job(id=2) arrives with a higher priority, 
  and job(id=1) arrives with a lower priority, the output in our sample output will be:

    2(-1) 4(0) 1(-1)  
  
  This function is not required and will not be graded. You may leave it
  blank if you do not find it useful.
 */
void scheduler_show_queue()
{
  
}
