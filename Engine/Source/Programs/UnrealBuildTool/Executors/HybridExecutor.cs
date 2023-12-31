// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using UnrealBuildTool.Artifacts;

namespace UnrealBuildTool
{
	/// <summary>
	/// Executor which distributes large sets of parallel actions through a remote executor, and any leaf serial actions through a local executor. Currently uses
	/// XGE or SNDBS for remote actions and ParallelExecutor for local actions.
	/// </summary>
	class HybridExecutor : ActionExecutor
	{
		/// <summary>
		/// Maximum number of actions to execute locally.
		/// </summary>
		int MaxLocalActions;

		/// <summary>
		/// Executor to use for remote actions
		/// </summary>
		readonly ActionExecutor RemoteExecutor;

		/// <summary>
		/// Executor to use for local actions
		/// </summary>
		readonly ActionExecutor LocalExecutor;

		/// <summary>
		/// Constructor
		/// </summary>
		public HybridExecutor(List<TargetDescriptor> TargetDescriptors, int InMaxLocalActions, bool bAllCores, bool bCompactOutput, ILogger Logger, ActionExecutor? InLocalExecutor = null, ActionExecutor? InRemoteExecutor = null)
			: base(Logger)
		{
			MaxLocalActions = InMaxLocalActions;
			LocalExecutor = InLocalExecutor ?? new ParallelExecutor(MaxLocalActions, bAllCores, bCompactOutput, Logger);
			RemoteExecutor = InRemoteExecutor ?? (XGE.IsAvailable(Logger) ? (ActionExecutor)new XGE(Logger) : new SNDBS(TargetDescriptors, Logger));

			XmlConfig.ApplyTo(this);

			if (MaxLocalActions == 0)
			{
				MaxLocalActions = Utils.GetPhysicalProcessorCount();
			}
		}

		protected override void Dispose(bool disposing)
		{
			base.Dispose(disposing);

			if (disposing)
			{
				LocalExecutor.Dispose();
				RemoteExecutor.Dispose();
			}
		}

		/// <summary>
		/// Tests whether this executor can be used
		/// </summary>
		/// <returns>True if the executor may be used</returns>
		public static bool IsAvailable(ILogger Logger)
		{
			return (XGE.IsAvailable(Logger) || SNDBS.IsAvailable(Logger)) && ParallelExecutor.IsAvailable();
		}

		/// <summary>
		/// Name of this executor for diagnostic output
		/// </summary>
		public override string Name => $"hybrid ({LocalExecutor.Name}+{RemoteExecutor.Name})";

		/// <inheritdoc/>
		public override async Task<bool> ExecuteActionsAsync(IEnumerable<LinkedAction> ActionsToExecute, ILogger Logger, IActionArtifactCache? actionArtifactCache)
		{
			if (!ActionsToExecute.Any())
			{
				return true;
			}

			// Find the number of dependants for each action
			Dictionary<LinkedAction, int> ActionToNumDependents = ActionsToExecute.ToDictionary(x => x, x => 0);
			foreach (LinkedAction Action in ActionsToExecute)
			{
				foreach (LinkedAction PrerequisiteAction in Action.PrerequisiteActions)
				{
					ActionToNumDependents[PrerequisiteAction]++;
				}
			}

			// Build up a set of leaf actions in several iterations, ensuring that the number of leaf actions in each 
			HashSet<LinkedAction> LeafActions = new HashSet<LinkedAction>();
			for (; ; )
			{
				// Find all the leaf actions in the graph
				List<LinkedAction> NewLeafActions = new List<LinkedAction>();
				foreach (LinkedAction Action in ActionsToExecute)
				{
					if (ActionToNumDependents[Action] == 0 && !LeafActions.Contains(Action))
					{
						NewLeafActions.Add(Action);
					}
				}

				// Exit once we can't prune any more layers from the tree
				if (NewLeafActions.Count == 0 || NewLeafActions.Count >= MaxLocalActions)
				{
					break;
				}

				// Add these actions to the set of leaf actions
				LeafActions.UnionWith(NewLeafActions);

				// Decrement the dependent counts for any of their prerequisites, so we can try and remove those from the tree in another iteration
				foreach (LinkedAction NewLeafAction in NewLeafActions)
				{
					foreach (LinkedAction PrerequisiteAction in NewLeafAction.PrerequisiteActions)
					{
						ActionToNumDependents[PrerequisiteAction]--;
					}
				}
			}

			// Split the list of actions into those which should be executed locally and remotely
			List<LinkedAction> LocalActionsToExecute = new List<LinkedAction>(LeafActions.Count);
			List<LinkedAction> RemoteActionsToExecute = new List<LinkedAction>(ActionsToExecute.Count() - LeafActions.Count);
			foreach (LinkedAction ActionToExecute in ActionsToExecute)
			{
				if (LeafActions.Contains(ActionToExecute))
				{
					LocalActionsToExecute.Add(ActionToExecute);
				}
				else
				{
					RemoteActionsToExecute.Add(ActionToExecute);
				}
			}

			// Execute the remote actions
			if (RemoteActionsToExecute.Count > 0 && !await RemoteExecutor.ExecuteActionsAsync(RemoteActionsToExecute, Logger, actionArtifactCache))
			{
				return false;
			}

			// Pass all the local actions through to the parallel executor
			if (LocalActionsToExecute.Count > 0 && !await LocalExecutor.ExecuteActionsAsync(LocalActionsToExecute, Logger, actionArtifactCache))
			{
				return false;
			}

			return true;
		}
	}
}
