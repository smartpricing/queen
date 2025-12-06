/**

libqueen
-------

struct Queen::Job 

struct Queen::JobStatus


class Queen::Core
initialize(
 db pool? 
)

custom types: 
- job: a struct that contiants all the job information
- job_status: a struct that contiants all the job status information, like type and backoff

class variables: 
- uv timers and handles 
- mutexes for thread safety
- job_queue: std::dequeu<job>
- job_status: std::map<std::string, job_status>

public methods: 
  submit(job, cb) 

internal methods:  
  loop(), dedicated thread


loop logic:
- get pending jobs feom queue
- for each job, check the status 
- if is non backoff job, execute it 
- if is a backoff job, check if we can execute it now 


*/