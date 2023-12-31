// Copyright Epic Games, Inc. All Rights Reserved.

using Horde.Build.Collections;
using Horde.Build.Models;
using Horde.Build.Utilities;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace Horde.Build.Tests.Stubs.Collections
{
	class StreamCollectionStub : IStreamCollection
	{
		public Task DeleteAsync(StreamId StreamId)
		{
			throw new NotImplementedException();
		}

		public Task<List<Stream>> FindAllAsync()
		{
			throw new NotImplementedException();
		}

		public Task<List<Stream>> FindForProjectAsync(ProjectId? ProjectId)
		{
			throw new NotImplementedException();
		}

		public Task<Stream> GetAsync(StreamId StreamId)
		{
			throw new NotImplementedException();
		}

		public Task<StreamPermissions> GetPermissionsAsync(StreamId StreamId)
		{
			throw new NotImplementedException();
		}

		public Task<bool> TryAddAsync(Stream Stream)
		{
			throw new NotImplementedException();
		}

		public Task<bool> TryUpdateCommitTimeAsync(Stream Stream, DateTime LastCommitTime)
		{
			throw new NotImplementedException();
		}

		public Task<bool> TryUpdatePropertiesAsync(Stream Stream, string NewName, int? NewOrder, List<StreamTab> NewTabs, Dictionary<string, AgentType> NewAgentTypes, Dictionary<string, WorkspaceType> NewWorkspaceTypes, Dictionary<StringId<TemplateRef>, TemplateRef> NewTemplateRefs, Dictionary<string, string> NewProperties, Acl NewAcl)
		{
			throw new NotImplementedException();
		}

		public Task<bool> TryUpdateScheduleJobsAsync(Stream Stream, TemplateId TemplateRefId, List<ObjectId> NewActiveJobs)
		{
			throw new NotImplementedException();
		}

		public Task<bool> TryUpdateScheduleTriggerTime(Stream Stream, TemplateId TemplateRefId, DateTimeOffset LastTriggerTime)
		{
			throw new NotImplementedException();
		}
	}
}
