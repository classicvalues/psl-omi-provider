/* This file contains a set of test WSMAN_PLUGIN_* APIs for carrying out
 * tests on the shell infrastructure.
 */

#include "ShellAPI.h"

typedef struct _PluginCommand PluginCommand;
typedef struct _PluginDetails PluginDetails;

typedef struct _PluginShell
{
	PluginDetails *pluginDetails;

	WSMAN_PLUGIN_REQUEST *shellRequestDetails;
	PluginCommand *command;
} PluginShell;

struct _PluginCommand
{
	WSMAN_PLUGIN_REQUEST *commandRequestDetails;
	WSMAN_PLUGIN_REQUEST *receiveRequestDetails;
	PluginShell *shell;
};

struct _PluginDetails
{
	PluginShell *shell;
};

DWORD WINAPI WSMAN_PLUGIN_STARTUP(
    _In_ DWORD flags,
    _In_ PCWSTR applicationIdentification,
    _In_opt_ PCWSTR extraInfo,
    _Out_ PVOID *pluginContext
    )
{
	PluginDetails *pluginDetails = calloc(1, sizeof(PluginDetails));
	if (pluginDetails == NULL)
	{
		return MI_RESULT_SERVER_LIMITS_EXCEEDED;
	}

	*pluginContext = pluginDetails;
	return MI_RESULT_OK;
}

/* Test plug-in API */
DWORD WINAPI WSMAN_PLUGIN_SHUTDOWN(
    _In_opt_ PVOID pluginContext,
    _In_ DWORD flags,
    _In_ DWORD reason
    )
{
	PluginDetails *pluginDetails = pluginContext;
	free(pluginDetails);
	return MI_RESULT_OK;
}

/* Test plug-in API */
VOID WINAPI WSMAN_PLUGIN_SHELL(
    _In_ PVOID pluginContext,   //Relates to context returned from WSMAN_PLUGIN_STARTUP
    _In_ WSMAN_PLUGIN_REQUEST *requestDetails,
    _In_ DWORD flags,
    _In_opt_ WSMAN_SHELL_STARTUP_INFO *startupInfo,
    _In_opt_ WSMAN_DATA *inboundShellInformation
    )
{
	PluginDetails *pluginDetails = pluginContext;
	PluginShell *shell = calloc(1, sizeof(PluginShell));
	if (shell == NULL)
	{
		WSManPluginOperationComplete(requestDetails, 0, MI_RESULT_SERVER_LIMITS_EXCEEDED, NULL);
	}
	else
	{
		pluginDetails->shell = shell;
		shell->shellRequestDetails = requestDetails;
		WSManPluginReportContext(requestDetails, flags, shell); /* Report the shell context */

		/*
		 * When the shell has completed we need to call:
		 * WSManPluginOperationComplete(requestDetails, 0, MI_RESULT_OK, NULL);
		 */
	}

}

VOID WINAPI WSMAN_PLUGIN_RELEASE_SHELL_CONTEXT(
    _In_ PVOID shellContext
    )
{
	PluginShell *shell = (PluginShell*) shellContext;
	PluginDetails *pluginDetails = shell->pluginDetails;

	free(shell);
	pluginDetails->shell = NULL;
}

/* Test plug-in API */
VOID WINAPI WSMAN_PLUGIN_COMMAND(
    _In_ WSMAN_PLUGIN_REQUEST *requestDetails,
    _In_ DWORD flags,
    _In_ PVOID shellContext,
    _In_ PCWSTR commandLine,
    _In_opt_ WSMAN_COMMAND_ARG_SET *arguments
    )
{
	PluginShell *shell = (PluginShell*) shellContext;

	shell->command = calloc(1, sizeof(PluginCommand));
	if (shell->command == NULL)
	{
		WSManPluginOperationComplete(requestDetails, 0, MI_RESULT_SERVER_LIMITS_EXCEEDED, NULL);
	}
	else
	{
		shell->command->commandRequestDetails = requestDetails;
		shell->command->shell = shell;

		WSManPluginReportContext(requestDetails, flags, shell->command); /* Report the command context */

		/*
		 * When the command has completed we need to call:
		 * WSManPluginOperationComplete(requestDetails, 0, MI_RESULT_OK, NULL);
		 */
	}
}

VOID WINAPI WSMAN_PLUGIN_RELEASE_COMMAND_CONTEXT(
    _In_ PVOID shellContext,
    _In_ PVOID commandContext
    )
{
	PluginShell *shell = (PluginShell*) shellContext;
	PluginCommand *command = (PluginCommand*) commandContext;

	free(command);
	shell->command = NULL;
}

VOID WINAPI WSMAN_PLUGIN_SEND(
    _In_ WSMAN_PLUGIN_REQUEST *requestDetails,
    _In_ DWORD flags,
    _In_ PVOID shellContext,
    _In_opt_ PVOID commandContext,
    _In_ PCWSTR stream,
    _In_ WSMAN_DATA *inboundData
    )
{
	PluginShell *shell = (PluginShell*) shellContext;
	PluginCommand *command = (PluginCommand*) commandContext;
	PCWSTR commandState;
	if (flags == WSMAN_FLAG_RECEIVE_RESULT_NO_MORE_DATA)
	{
		commandState = WSMAN_COMMAND_STATE_DONE;
	}
	else
	{
		commandState = WSMAN_COMMAND_STATE_RUNNING;
	}

    /* Wait for a Receive request to come in before we post the result back */
    do
    {

    } while (CondLock_Wait((ptrdiff_t)&command->receiveRequestDetails, (ptrdiff_t*)&command->receiveRequestDetails, 0, CONDLOCK_DEFAULT_SPINCOUNT) == 0);

	/* Send the data back to the client for the pending Receive call */
	WSManPluginReceiveResult(command->receiveRequestDetails, 0, stream, inboundData, commandState, 0);

	if (flags == WSMAN_FLAG_RECEIVE_RESULT_NO_MORE_DATA)
	{
		WSManPluginOperationComplete(command->receiveRequestDetails, 0, MI_RESULT_OK, NULL);
		command->receiveRequestDetails = NULL;
	}

	/* Complete the send request */
	WSManPluginOperationComplete(requestDetails, 0, MI_RESULT_OK, NULL);
}

VOID WINAPI WSMAN_PLUGIN_RECEIVE(
    _In_ WSMAN_PLUGIN_REQUEST *requestDetails,
    _In_ DWORD flags,
    _In_ PVOID shellContext,
    _In_opt_ PVOID commandContext,
    _In_opt_ WSMAN_STREAM_ID_SET *streamSet
    )
{
	PluginShell *shell = (PluginShell*) shellContext;
	PluginCommand *command = (PluginCommand*) commandContext;

	command->receiveRequestDetails = requestDetails;

    CondLock_Broadcast((ptrdiff_t) &command->receiveRequestDetails);


	/* Call the ReceiveResults call in the Send function to send back what we received
	 * WSManPluginReceiveResult(requestDetails, 0, "stream", streamResult, commandState, exitCode);
	 *
	 * When done call
	 * WSManPluginOperationComplete(requestDetails, 0, 0, NULL);
	 */

}

VOID WINAPI WSMAN_PLUGIN_SIGNAL(
    _In_ WSMAN_PLUGIN_REQUEST *requestDetails,
    _In_ DWORD flags,
    _In_ PVOID shellContext,
    _In_opt_ PVOID commandContext,
    _In_ PCWSTR code
    )
{
	/* If the operation has finished we need to finish receives and close all commands and shells as
	 * appropriate
	 */
	WSManPluginOperationComplete(requestDetails, 0, 0, NULL);
}

VOID WINAPI WSMAN_PLUGIN_CONNECT(
    _In_ WSMAN_PLUGIN_REQUEST *requestDetails,
    _In_ DWORD flags,
    _In_ PVOID shellContext,
    _In_opt_ PVOID commandContext,
    _In_opt_ WSMAN_DATA *inboundConnectInformation
     )
{
	WSManPluginOperationComplete(requestDetails, 0, MI_RESULT_NOT_SUPPORTED, NULL);
}