# Requirements Clarification

## Question 1

What timer behavior should the new Kennel job support: a one-time delayed execution, a recurring schedule (for example, every 5 minutes or a cron expression), or both?

## Answer 1

The job should support a recurring schedule.

## Question 2

What schedule format should users configure for recurring execution: a fixed interval (for example, every 5 minutes), a cron expression, or support for both?

## Answer 2

The recurring schedule should support both:

- Calendar-based schedules, such as every Sunday at 16:00 or every day at 11:00.
- Fixed intervals, such as every 5 minutes.

## Question 3

What should the AI prompt be able to access when it runs—for example, only the configured prompt text, or also Kennel job data, previous run results, files, external services, or other context?

## Answer 3

The AI prompt should have access to:

- The configured prompt text.
- Files.
- Every MCP tool available to the Kennel environment.

## Question 4

How should the scheduled AI job handle its output: should it only record the AI response in the job run history, or should it also support delivering the response to a configured destination such as a file, webhook, message, or another Kennel job?

## Answer 4

Output handling should be determined by the individual prompt and the MCP tools available to the AI. For example, a prompt may instruct the AI to check the Kennel GitHub project for newly opened issues and send a Slack message when appropriate. The job should not require a separate fixed output destination for every prompt.

## Question 5

What permissions and safety controls should apply when the AI uses MCP tools—for example, should scheduled jobs be allowed to call all available tools automatically, or should tool access and potentially destructive actions require explicit per-job approval or configuration?

## Answer 5

Each scheduled job will be configured with an explicit allowlist of safe MCP tools. For example, a job may be permitted to use read-only tools plus the Slack MCP. Tools outside the allowlist must not be available to that job.

## Question 6

What should happen when a scheduled run fails—for example, because an MCP tool or AI provider is unavailable: should Kennel retry automatically, and if so, how many times and with what delay?

## Answer 6

Retry behavior should be configurable per scheduled job. If an error occurs repeatedly, the job should be able to notify an operator through a configured channel, such as email or Slack.

## Question 7

How should repeated failures be defined and controlled: should the job notify after a configurable number of consecutive failed runs, and should it stop/pause scheduling after that threshold or continue retrying on future scheduled runs?

## Answer 7

The repeated-failure threshold and resulting behavior should be configurable per scheduled job.

## Question 8

After the configured repeated-failure threshold is reached, should the job continue running on its normal schedule, pause automatically until manually resumed, or allow either behavior to be selected per job?

## Answer 8

The post-threshold behavior should be configurable per job. Each job may be configured either to continue on its normal schedule or pause automatically until manually resumed.

## Question 9

What timezone should calendar-based schedules use: a single system-wide timezone, the creating user's timezone, or a timezone selected explicitly in each job's configuration?

## Answer 9

Calendar-based schedules should use the current machine timezone.

## Question 10

What job lifecycle controls are required for scheduled prompt jobs: should users be able to create, edit, enable/disable, run immediately, and delete jobs, or is a smaller set sufficient for the first version?

## Answer 10

The first version should support the complete job lifecycle:

- Create jobs.
- Edit jobs.
- Enable and disable jobs.
- Run jobs immediately.
- Delete jobs.

## Question 11

How should the scheduler handle overlapping runs when a job's next scheduled time arrives while its previous AI execution is still running: prevent overlap and skip/queue the new run, or allow concurrent executions?

## Answer 11

Overlapping executions are not allowed. If a job is still running when its next scheduled time arrives, the scheduler should skip the new occurrence rather than queueing it or running concurrently.

## Question 12

What run history and observability should be available: should Kennel record each run's start/end time, status, error details, AI response, tools invoked, and skipped occurrences, and should users be able to view or export that history?

## Answer 12

Record every execution using the existing log-file mechanism, with a new `jobs.log` log. The execution record should support run history and observability, including execution outcomes and relevant details such as timing, errors, AI responses, tool activity, and skipped occurrences where supported by the logging mechanism.

## Question 13

Which AI runtime should scheduled prompt jobs use: the existing AI provider/model configuration in Kennel, a provider and model selected per job, or both with a configurable per-job override?

## Answer 13

The AI runtime should be configured per job. Users should choose the runtime from an available list, such as Claude Code, Codex, Kiro CLI, and similar supported tools. This runtime selection is separate from the per-job MCP-tool allowlist.

## Question 14

For each selected AI runtime, should the job configuration also allow users to choose a specific model and runtime options, or should the first version use each runtime's default model and options?

## Answer 14

Users should be able to configure command-line arguments for the selected AI runtime. For example, a Claude Code job could invoke:

```sh
claude --model Haiku -p "How are you?"
```

The job configuration must support passing runtime-specific arguments, including model selection where the runtime exposes it.

## Question 15

How should the prompt be passed to the selected runtime: should Kennel always append or inject the configured prompt as the final prompt argument (while users configure only the runtime executable and options), or should users configure the complete command template themselves?

## Answer 15

The job configuration will contain a separate `Prompt` field and a selected runtime executable, such as `claude`. Users may provide additional runtime parameters, such as `--model "Haiku"`. Kennel will construct the command line from the selected runtime, configured parameters, and prompt rather than requiring users to define a complete command template.

## Question 16

Because scheduled jobs construct and execute external commands, what validation and security policy should apply to runtime selection and arguments: should users choose only from a predefined runtime allowlist and have arguments safely passed without shell interpolation, with prompt and argument values treated as untrusted input?

## Answer 16

AI runtimes must be selected from a predefined list. Runtime selection is not an arbitrary executable path or free-form command.

## Question 17

Should command-line arguments be entered as a structured list of individual arguments (recommended for safe execution), or as one free-form argument string that Kennel parses?

## Answer 17

Command-line arguments should be entered as a structured list of individual arguments. Kennel should pass them directly to the selected runtime without parsing a free-form shell command string.

## Question 18

What should be the initial configuration and management interface for scheduled jobs: a command-line interface, a web/UI interface, configuration files, or support for multiple interfaces?

## Answer 18

The initial feature should provide the underlying infrastructure and APIs/services rather than a finished management UI. A UI will be added later to wrap the infrastructure and expose job lifecycle management.

## Question 19

What persistence mechanism should scheduled job definitions use: Kennel's existing configuration/storage mechanism, a new dedicated jobs database/file, or should the implementation first expose an in-memory/service abstraction and defer durable persistence?

## Answer 19

Scheduled job definitions should use JSON persistence.

## Question 20

Where should the JSON job definitions be stored, and should Kennel support one shared jobs file or a configurable path for the jobs file?

## Answer 20

Store the shared job definitions in `~/.kennel/jobs.json`, alongside Kennel's other JSON files.

## Question 21

What should happen when the machine is offline or unavailable at a scheduled time: should the scheduler skip missed occurrences and resume at the next scheduled time, or attempt to catch up on missed runs when it becomes available?

## Answer 21

The scheduler is available only while Kennel is running. Missed occurrences must be ignored; the scheduler should resume with the next future occurrence rather than catch up.

## Question 22

For a recurring job configured with a fixed interval, should the interval be measured from the scheduled trigger time or from the completion time of the previous run (with skipped overlapping occurrences still recorded)?

## Answer 22

Defer the fixed-interval anchoring semantics to phase 2 and leave this decision out of the current scope.

## Question 23

For the initial infrastructure phase, should fixed-interval scheduling itself remain included with a documented default behavior, or should phase 1 support only calendar-based schedules and defer fixed intervals entirely to phase 2?

## Answer 23

Phase 1 should support only calendar-based schedules. Fixed-interval scheduling and its anchoring semantics are deferred entirely to phase 2.

## Question 24

For phase 1 calendar schedules, should the schedule support daily execution and selected weekdays (for example, every day at 11:00 or every Sunday at 16:00), with one execution time per job and the machine's local timezone?

## Answer 24

Phase 1 calendar schedules should support daily execution and selected weekdays, with one execution time per job, using the machine's local timezone.

## Question 25

How should files be made available to the AI runtime: should the job receive a configured list of file paths/directories as context, or should the prompt be able to refer to files through the runtime's existing working-directory and file tools without a separate file list?

## Answer 25

The prompt itself should specify the files or directories to inspect, including explicit paths when needed. For example: `Check the source files in /home/eran/devl/codelite and see if anything was changed ...`. Phase 1 does not need a separate file-path configuration field; file access is governed by the selected runtime and permitted tools.

## Question 26

What should the job's execution working directory be: a configurable per-job directory, Kennel's current working directory, or the user's home/Kennel directory by default?

## Answer 26

The execution working directory should be configurable per job.

## Question 27

Should the per-job working directory be required, or should Kennel provide a default (such as the current working directory) when it is omitted?

## Answer 27

The per-job working directory is optional. If omitted, it defaults to the process working directory.

## Question 28

For the phase 1 job definition, should the required/optional fields be: name, prompt, runtime, structured runtime arguments, schedule (daily or selected weekdays plus time), allowed MCP tools, working directory, retry policy, repeated-failure notification policy, and post-threshold action?

## Answer 28

Keep phase 1 deliberately simple. Remove retry policy, repeated-failure notification policy, and post-threshold action. A job runs when scheduled (or manually triggered); if execution fails, Kennel records the failure in `jobs.log`. No retries, notifications, or automatic pausing are required.

## Requirements Clarification Checkpoint

The current phase 1 scope has been captured. Do you feel requirements clarification is complete, or should we clarify another requirement before moving to research? Research can investigate Kennel's existing job, scheduler, JSON persistence, logging, runtime, and MCP-tool integration patterns.

