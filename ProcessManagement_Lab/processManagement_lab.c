#include "processManagement_lab.h"

/**
 * The task function to simulate "work" for each worker process
 * TODO#3: Modify the function to be multiprocess-safe 
 * */
void task(long duration)
{
    // simulate computation for x number of seconds
    usleep(duration*TIME_MULTIPLIER);

    // TODO: protect the access of shared variable below
    sem_wait(sem_global_data);

    // update global variables to simulate statistics
    ShmPTR_global_data->sum_work += duration;
    ShmPTR_global_data->total_tasks ++;
    if (duration % 2 == 1) {
        // printf("%ld\n",duration);
        ShmPTR_global_data->odd++;
    }
    if (duration < ShmPTR_global_data->min) {
        ShmPTR_global_data->min = duration;
    }
    if (duration > ShmPTR_global_data->max) {
        ShmPTR_global_data->max = duration;
    }

    sem_post(sem_global_data);
}


/**
 * The function that is executed by each worker process to execute any available job given by the main process
 * */
void job_dispatch(int i){
    //printf("Hello from child %d with pid %d and parent id %d\n", i, getpid(), getppid());
    //exit(0); 

    while(true){

        sem_wait(sem_jobs_buffer[i]);

        if(shmPTR_jobs_buffer[i].task_status != 0){
            if(shmPTR_jobs_buffer[i].task_type == 't'){
                // printf("%d process ID doing %c %d\n",getpid(),shmPTR_jobs_buffer[i].task_type,shmPTR_jobs_buffer[i].task_duration);
                task(shmPTR_jobs_buffer[i].task_duration);
                shmPTR_jobs_buffer[i].task_status = 0;
                shmPTR_jobs_buffer[i].task_duration = 0;
            }
            else if(shmPTR_jobs_buffer[i].task_type == 'w'){
                // printf("%d process ID doing %c \n",getpid(),shmPTR_jobs_buffer[i].task_type);
                usleep(shmPTR_jobs_buffer[i].task_duration * TIME_MULTIPLIER);
                shmPTR_jobs_buffer[i].task_status = 0;
                shmPTR_jobs_buffer[i].task_duration = 0;
            }
            else if(shmPTR_jobs_buffer[i].task_type == 'z'){
                // printf("%d process ID doing %c \n",getpid(),shmPTR_jobs_buffer[i].task_type);
                exit(3);
                shmPTR_jobs_buffer[i].task_status = 0;
            }
            else if(shmPTR_jobs_buffer[i].task_type == 'i'){
                // printf("%d process ID doing %c %d\n",getpid(),shmPTR_jobs_buffer[i].task_type,shmPTR_jobs_buffer[i].task_duration);
                shmPTR_jobs_buffer[i].task_status = 0;
                kill(getpid(),SIGKILL);
            }
        }
    }
}

/** 
 * Setup function to create shared mems and semaphores
 * **/
void setup(){

    ShmID_global_data = shmget(IPC_PRIVATE, sizeof(global_data), IPC_CREAT | 0666);
    if (ShmID_global_data == -1){
        // printf("Global data shared memory creation failed\n");
        exit(EXIT_FAILURE);
    }
    ShmPTR_global_data = (global_data *) shmat(ShmID_global_data, NULL, 0);
    if ((int) ShmPTR_global_data == -1){
        // printf("Attachment of global data shared memory failed \n");
        exit(EXIT_FAILURE);
    }

    //set global data min and max
    ShmPTR_global_data->max = -1;
    ShmPTR_global_data->min = INT_MAX;

    /*
    Part C
    */

    //Semaphore for global_data struct
    sem_global_data = sem_open("semglobaldata",O_CREAT|O_EXCL,0644,1);

    //Check if sempahore is correctly opened
    while(true){
        if(sem_global_data == SEM_FAILED){
            sem_unlink("semglobaldata");
            sem_global_data = sem_open("semglobaldata",O_CREAT|O_EXCL,0644,1);
        }
        else{
            break;
        }
    }

    /*
    Part D
    */

   //Create shared memory for number_of_processes job struct
   ShmID_jobs = shmget(IPC_PRIVATE, sizeof(job)*number_of_processes, IPC_CREAT | 0666);
   if(ShmID_jobs == -1){
    //    printf("Job data shared memory creation failed\n");
       exit(EXIT_FAILURE);
   }
   shmPTR_jobs_buffer = (job *) shmat(ShmID_jobs,NULL,0);
   if((int) shmPTR_jobs_buffer == -1){
    //    printf("Attachment to job data shared memeory failed\n");
       exit(EXIT_FAILURE);
   }

   /*
   Part E
   */

    //Loop to setup contents of job buffer
    for(int i=0;i<number_of_processes;i++){
        shmPTR_jobs_buffer[i].task_status=0;
        shmPTR_jobs_buffer[i].task_duration=0;
    }

    /*
    Part F
    */

    //Semaphore for number_of_processes
    for(int i=0;i<number_of_processes;i++){
        char *sem_name = malloc(sizeof(char)*16);
        sprintf(sem_name, "semjobs%d", i);
        sem_jobs_buffer[i] = sem_open(sem_name,O_CREAT|O_EXCL,0644,1);
        while(true){
            if(sem_jobs_buffer[i] == SEM_FAILED){
                sem_unlink("semjobsi");
                sem_jobs_buffer[i] = sem_open("semjobsi",O_CREAT|O_EXCL,0644,1);
            }
            else{
                break;
            }
        }
    }

    return;

}

/**
 * Function to spawn all required children processes
 **/
 
void createchildren(){
    /*
    Task A
    */
    for(int i = 0; i<number_of_processes; i++){
        int temp_pid = fork();          //Child process id sent to parent, so use in else
        if (temp_pid<0){
            printf("%d child was not created", i);
        }
        else if (temp_pid == 0){
            job_dispatch(i);
            break;
        }
        else{
            children_processes[i] = temp_pid;
        }
    }

    return;
}

/**
 * The function where the main process loops and busy wait to dispatch job in available slots
 * */
void main_loop(char* fileName){

    // load jobs and add them to the shared memory
    FILE* opened_file = fopen(fileName, "r");
    char action; //stores whether its a 'p' or 'w'
    long num; //stores the argument of the job 

    while (fscanf(opened_file, "%c %ld\n", &action, &num) == 2) { //while the file still has input

        // printf("%c %ld\n",action,num);
        bool jobAssigned = false;
        while(true){
            for(int i =0; i<number_of_processes; i++){
                int alive = waitpid(children_processes[i],NULL,WNOHANG);
                if(alive==0){
                    if(shmPTR_jobs_buffer[i].task_status == 0){
                        jobAssigned = true;
                        shmPTR_jobs_buffer[i].task_type = action;
                        shmPTR_jobs_buffer[i].task_duration = num;
                        shmPTR_jobs_buffer[i].task_status = 1;
                        sem_post(sem_jobs_buffer[i]);
                    }
                }
                else{
                    pid_t temp_pid = fork();          //Child process id sent to parent, so use in else
                    if (temp_pid<0){
                        // printf("%d child was not created", i);
                    }
                    else if (temp_pid == 0){
                        job_dispatch(i);
                    }
                    else{
                        children_processes[i] = temp_pid;
                    }
                }
                if(jobAssigned){break;}
            }
            if(jobAssigned){break;}
        }
    }
    fclose(opened_file);
    //sleep(3);

    // printf("Main process is going to send termination signals\n");

    // TODO#4: Design a way to send termination jobs to ALL worker that are currently alive 
    int deadProcesses = 0;
    while(true){
        for(int i =0; i<number_of_processes; i++){
            int alive = waitpid(children_processes[i],NULL,WNOHANG);
            if(alive==0 && shmPTR_jobs_buffer[i].task_status==0){
                // printf("killing one\n");
                shmPTR_jobs_buffer[i].task_type = 'z';
                shmPTR_jobs_buffer[i].task_duration = 0;
                shmPTR_jobs_buffer[i].task_status = 1; //new, undone job
                sem_post(sem_jobs_buffer[i]); // signal the child
                deadProcesses++;
                // printf("%d\n",deadProcesses);
            }
            else if(alive>0){
                shmPTR_jobs_buffer[i].task_type = 'z';
                shmPTR_jobs_buffer[i].task_duration = 0;
                shmPTR_jobs_buffer[i].task_status = 1; //new, undone job
                sem_post(sem_jobs_buffer[i]); // signal the child
                deadProcesses++;
            }
        }
        if(deadProcesses==number_of_processes){break;}
    }

    //printf("Termination sigansl done \n");
    //wait for all children processes to properly execute the 'z' termination jobs
    int process_waited_final = 0;
    pid_t wpid = wait(NULL);
    while ((wpid = wait(NULL)) > 0){
        process_waited_final ++;
    }
    // print final results
    printf("Final results: sum -- %ld, odd -- %ld, min -- %ld, max -- %ld, total task -- %ld\n", ShmPTR_global_data->sum_work, ShmPTR_global_data->odd, ShmPTR_global_data->min, ShmPTR_global_data->max, ShmPTR_global_data->total_tasks);
}

void cleanup(){
    //TODO#4: 
    // 1. Detach both shared memory (global_data and jobs)
    // 2. Delete both shared memory (global_data and jobs)
    // 3. Unlink all semaphores in sem_jobs_buffer

    //detach and remove shared memory locations
    int detach_status = shmdt((void *) ShmPTR_global_data); //detach
    if (detach_status == -1){ 
        // printf("Detach shared memory global_data ERROR\n");
    }
    int remove_status = shmctl(ShmID_global_data, IPC_RMID, NULL); //delete
    if (remove_status == -1){
        // printf("Remove shared memory global_data ERROR\n");
    }
    detach_status = shmdt((void *) shmPTR_jobs_buffer); //detach
    if (detach_status == -1){ 
        // printf("Detach shared memory jobs ERROR\n");
    }
    remove_status = shmctl(ShmID_jobs, IPC_RMID, NULL); //delete
    if (remove_status == -1) {
        // printf("Remove shared memory jobs ERROR\n");
    }

    //unlink all semaphores before exiting process
    int sem_close_status = sem_unlink("semglobaldata");
    if (sem_close_status == 0){
        // printf("Semaphore globaldata closes succesfully.\n");
    }
    else{
        // printf("Semaphore globaldata fails to close.\n");
    }

    for (int i = 0; i<number_of_processes; i++){
        char *sem_name = malloc(sizeof(char)*16);
        sprintf(sem_name, "semjobs%d", i);
        sem_close_status = sem_unlink(sem_name);
        if (sem_close_status == 0){
            //  printf("Semaphore jobs %d closes succesfully.\n", i);
        }
        else{
            // printf("Semaphore jobs %d fails to close.\n", i);
        }
        free(sem_name);
    }

}

// Real main
int main(int argc, char* argv[]){

    // printf("Lab 1 Starts...\n");

    struct timeval start, end;
    long secs_used,micros_used;

    //start timer
    gettimeofday(&start, NULL);

    //Check and parse command line options to be in the right format
    if (argc < 2) {
        // printf("Usage: sum <infile> <numprocs>\n");
        exit(EXIT_FAILURE);
    }


    //Limit number_of_processes into 10. 
    //If there's no third argument, set the default number_of_processes into 1.  
    if (argc < 3){
        number_of_processes = 1;
    }
    else{
        if (atoi(argv[2]) < MAX_PROCESS) number_of_processes = atoi(argv[2]);
        else number_of_processes = MAX_PROCESS;
    }

    setup();
    createchildren();
    main_loop(argv[1]);

    //parent cleanup
    cleanup();

    //stop timer
    gettimeofday(&end, NULL);

    double start_usec = (double) start.tv_sec * 1000000 + (double) start.tv_usec;
    double end_usec =  (double) end.tv_sec * 1000000 + (double) end.tv_usec;

    printf("Your computation has used: %lf secs \n", (end_usec - start_usec)/(double)1000000);


    return (EXIT_SUCCESS);
}