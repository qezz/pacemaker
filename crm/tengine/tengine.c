#include <crm/crm.h>
#include <crm/msg_xml.h>
#include <crm/common/msgutils.h>
#include <crm/common/xmlutils.h>
#include <crm/cib.h>
#include <tengine.h>

GSListPtr graph = NULL;
IPC_Channel *crm_ch = NULL;

typedef struct action_list_s 
{
		gboolean force;
		int index;
		int index_max;
		GSListPtr actions;
} action_list_t;

void print_state(void);
gboolean initialize_graph(void);
gboolean unpack_graph(xmlNodePtr xml_graph);
gboolean extract_event(xmlNodePtr msg);
gboolean initiate_transition(void);
gboolean initiate_action(action_list_t *list);
gboolean process_graph_event(const char *event_node,
			     const char *event_rsc, 
			     const char *event_action, 
			     const char *event_status, 
			     const char *event_rc);

void send_success(void);
void send_abort(xmlNodePtr msg);
gboolean process_fake_event(xmlNodePtr msg);


gboolean
initialize_graph(void)
{
	while(g_slist_length(graph) > 0) {
		action_list_t *action_list = g_slist_nth_data(graph, 0);
		while(g_slist_length(action_list->actions) > 0) {
			xmlNodePtr action =
				g_slist_nth_data(action_list->actions, 0);
			action_list->actions =
				g_slist_remove(action_list->actions, action);
			free_xml(action);
		}
		graph = g_slist_remove(graph, action_list);
		cl_free(action_list);
	}

	graph = NULL;
	
	return TRUE;
}


gboolean
unpack_graph(xmlNodePtr xml_graph)
{
/*
<transition_graph>
	<actions id="0">
		<rsc_op id="5" runnable="false" optional="true" task="stop">
			<resource id="rsc3" priority="3.0"/>
		</rsc_op>
*/
	xmlNodePtr xml_action_list = xml_graph?xml_graph->children:NULL;
	if(xml_action_list == NULL) {
		// nothing to do
		return FALSE;
	}
	
	while(xml_action_list != NULL) {
		xmlNodePtr xml_obj = xml_action_list;
		xmlNodePtr xml_action = xml_obj->children;
		action_list_t *action_list = (action_list_t*)
			cl_malloc(sizeof(action_list_t));

		xml_action_list = xml_action_list->next;

		action_list->force = FALSE;
		action_list->index = -1;
		action_list->index_max = 0;
		action_list->actions = NULL;
		
		while(xml_action != NULL) {
			xmlNodePtr action =
				copy_xml_node_recursive(xml_action);

			action_list->actions =
				g_slist_append(action_list->actions, action);
			
			action_list->index_max++;
			xml_action = xml_action->next;
		}
		
		graph = g_slist_append(graph, action_list);
	}
	

	return TRUE;
}

gboolean
process_fake_event(xmlNodePtr msg)
{
	
	xmlNodePtr data = find_xml_node(msg, "lrm_resource");
	CRM_DEBUG("Processing fake LRM event...");
	const char *last_op = "start";
	if(safe_str_eq("stopped", xmlGetProp(data, "last_op"))) {
		last_op = "stop";
	}
	CRM_DEBUG("Fake LRM event... %s, %s, %s, %s, %s",
		  xmlGetProp(data, "op_node"),
		  xmlGetProp(data, "id"),
		  last_op,
		  xmlGetProp(data, "op_status"),
		  xmlGetProp(data, "op_code"));
	
	return process_graph_event(xmlGetProp(data, "op_node"),
				   xmlGetProp(data, "id"),
				   xmlGetProp(data, "last_op"),
				   xmlGetProp(data, "op_status"),
				   xmlGetProp(data, "op_code"));
}

gboolean
extract_event(xmlNodePtr msg)
{
	gboolean abort      = FALSE;
	xmlNodePtr iter     = NULL;
	const char *section = NULL;

	const char *event_action = NULL;
	const char *event_node   = NULL;
	const char *event_rsc    = NULL;
	const char *event_status = NULL;
	const char *event_rc     = NULL;
	
/*
[cib fragment]
...
<status>
   <node_state id="node1" state="active" exp_state="active">
     <lrm>
       <lrm_resources>
	 <rsc_state id="" rsc_id="rsc4" node_id="node1" rsc_state="stopped"/>
*/

	xml_message_debug(msg, "TE Event");
	
	iter = find_xml_node(msg, XML_TAG_FRAGMENT);
	section = xmlGetProp(iter, "section");

	if(safe_str_neq(section, XML_CIB_TAG_STATUS)) {
		// these too are never expected
		return FALSE;
	}
	
	iter = find_xml_node(iter, XML_TAG_CIB);
	iter = get_object_root(XML_CIB_TAG_STATUS, iter);
	iter = iter->children;
	
	while(abort == FALSE && iter != NULL) {
		xmlNodePtr node_state = iter;
		xmlNodePtr child = iter->children;
		const char *state = xmlGetProp(node_state, "state");
		iter = iter->next;

		if(state != NULL && child == NULL) {
			/* node state update,
			 * possibly from a shutdown we requested
			 */
			CRM_DEBUG("state no child");
			
			event_status = state;
			event_node   = xmlGetProp(node_state, XML_ATTR_ID);
			if(safe_str_eq(event_status, "down")) {
				event_action = "shutdown";
			}
			
			abort = !process_graph_event(event_node,
						     event_rsc,
						     event_action,
						     event_status,
						     event_rc);

		} else if(state != NULL && child != NULL) {
			/* this is a complex event and could not be completely
			 * due to any request we made
			 */
			abort = TRUE;
			CRM_DEBUG("state and child");
			
		} else {
			CRM_DEBUG("child no state");
			child = find_xml_node(node_state, "lrm");
			child = find_xml_node(child, "lrm_resources");

			if(child != NULL) {
				child = child->children;
			} else {
				abort = TRUE;
			}
			
			while(abort == FALSE && child != NULL) {
				event_action = xmlGetProp(child, "last_op");
				event_node   = xmlGetProp(child, "op_node");
				event_rsc    = xmlGetProp(child, "id");
				event_status = xmlGetProp(child, "op_status");
				event_rc     = xmlGetProp(child, "op_code");
				
				abort = !process_graph_event(event_node,
							     event_rsc,
							     event_action,
							     event_status,
							     event_rc);

				child = child->next;
			}	
		}
	}
	
	return !abort;
}


gboolean
process_graph_event(const char *event_node,
		    const char *event_rsc, 
		    const char *event_action, 
		    const char *event_status, 
		    const char *event_rc)
{
	int lpc;
	xmlNodePtr action        = NULL; // <rsc_op> or <crm_event>
	xmlNodePtr next_action   = NULL;
	action_list_t *matched_action_list = NULL;

// Find the action corresponding to this event
	slist_iter(
		action_list, action_list_t, graph, lpc,
		action = g_slist_nth_data(action_list->actions,
					  action_list->index);

		if(action == NULL) {
			continue;
		}
/*
		<rsc_op id= runnable= optional= task= on_node= >
			<resource id="rsc3" priority="3.0"/>
		</rsc_op>
*/
		const char *this_action = xmlGetProp(action, "task");
		const char *this_node   = xmlGetProp(action, "on_node");
		const char *this_rsc    = xmlGetProp(action->children, "id");

		if(safe_str_neq(this_node, event_node)) {
			continue;

		} else if(safe_str_neq(this_action, event_action)) {
			continue;
			
		} else if(safe_str_eq(action->name, "rsc_op")
			  && safe_str_eq(this_rsc, event_rsc)) {
			matched_action_list = action_list;

		} else if(safe_str_eq(action->name, "crm_event")) {
			matched_action_list = action_list;
		}
		);			

	// for the moment all actions succeed
	
	if(matched_action_list == NULL) {
		// unexpected event, trigger a pe-recompute
		// possibly do this only for certain types of actions
		cl_log(LOG_ERR,
		       "Unexpected event... matched action list was NULL");
		return FALSE;
	}

	while(matched_action_list->index <= matched_action_list->index_max) {
		next_action = g_slist_nth_data(matched_action_list->actions,
					       matched_action_list->index);

		gboolean passed = initiate_action(matched_action_list);

		if(passed == FALSE) {
			cl_log(LOG_ERR,
			       "Initiation of next event failed");
			return FALSE;
			
		} else if(matched_action_list->index >
			  matched_action_list->index_max) {
			/* last action in that list, check if there are
			 *  anymore actions at all
			 */
			slist_iter(
				action_list, action_list_t, graph, lpc,
				if(action_list->index <=
				   action_list->index_max){
					return TRUE;
				}
				);
		} else {
			return TRUE;
			
		}

	}
	cl_log(LOG_INFO, "Transition complete...");

	send_success();
	
	return TRUE;
}

gboolean
initiate_transition(void)
{
	int lpc;
	gboolean anything = FALSE;

	FNIN();
	
	slist_iter(
		action_list, action_list_t, graph, lpc,
		if(initiate_action(action_list)) {
			anything = TRUE;
		}
		);

	FNRET(anything);
}

gboolean
initiate_action(action_list_t *list) 
{
	gboolean is_optional  = TRUE;
	xmlNodePtr xml_action = NULL;
	const char *on_node   = NULL;
	const char *id        = NULL;
	const char *runnable  = NULL;
	const char *optional  = NULL;
	const char *task      = NULL;
	
	while(TRUE) {
		
		list->index++;
		xml_action = g_slist_nth_data(list->actions, list->index);
		
		if(xml_action == NULL) {
			cl_log(LOG_INFO, "No tasks left on this list");
			list->index = list->index_max + 1;
			
			return TRUE;
		}
		
		
		on_node  = xmlGetProp(xml_action, "on_node");
		id       = xmlGetProp(xml_action, "id");
		runnable = xmlGetProp(xml_action, "runnable");
		optional = xmlGetProp(xml_action, "optional");
		task     = xmlGetProp(xml_action, "task");
		
		if(safe_str_neq(optional, "true")) {
			is_optional = FALSE;
		}
		
		list->force = list->force || !is_optional;

		/*
		cl_log(LOG_DEBUG,
		       "Processing action %s (id=%s) on %s",
		       task, id, on_node);
		*/
		
		if(list->force && is_optional) {
			cl_log(LOG_INFO,
			       "Forcing execution of otherwise optional task "
			       "due to a dependancy on a previous action");
		}
		
		if(list->force == FALSE && is_optional) {
			if(safe_str_eq(xml_action->name, "rsc_op")){
				cl_log(LOG_INFO,
				       "Skipping rsc-op (%s): %s %s on %s",
				       id, task,
				       xmlGetProp(xml_action->children, "id"),
				       on_node);
			} else {
				cl_log(LOG_INFO,
				       "Skipping optional command %s (id=%s) on %s",
				       task, id, on_node);
			}
			
		} else if(safe_str_eq(runnable, "false")) {
			cl_log(LOG_ERR,
			       "Failed on un-runnable command: %s (id=%s) on %s",
			       task, id, on_node);
			return FALSE;
			
		} else if(id == NULL || strlen(id) == 0
			  || on_node == NULL || strlen(on_node) == 0
			  || task == NULL || strlen(task) == 0) {
			// error
			cl_log(LOG_ERR,
			       "Failed on corrupted command: %s (id=%s) on %s",
			       task, id, on_node);
			
			return FALSE;
			
//	} else if(safe_str_eq(xml_action->name, "pseduo_event")){
			
		} else if(safe_str_eq(xml_action->name, "crm_event")){
			/*
			  <crm_msg op="task" to="on_node">
			*/
			cl_log(LOG_INFO,
			       "Executing crm-event (%s): %s on %s",
			       id, task, on_node);
#ifndef TESTING
			xmlNodePtr options = create_xml_node(NULL, "options");
			set_xml_property_copy(options, XML_ATTR_OP, task);
			
			send_ipc_request(crm_ch, options, NULL,
					 on_node, "crmd", "tengine",
					 NULL, NULL);
			
			free_xml(options);
			return TRUE;
#endif			
		} else if(safe_str_eq(xml_action->name, "rsc_op")){
			cl_log(LOG_INFO,
			       "Executing rsc-op (%s): %s %s on %s",
			       id, task,
			       xmlGetProp(xml_action->children, "id"),
			       on_node);
#ifndef TESTING
			/*
			  <msg_data>
			  <rsc_op id="operation number" on_node="" task="">
			  <resource>...</resource>
			*/
			xmlNodePtr options = create_xml_node(NULL, "options");
			xmlNodePtr data = create_xml_node(NULL, "msg_data");
			xmlNodePtr rsc_op = create_xml_node(data, "rsc_op");
			
			set_xml_property_copy(options, XML_ATTR_OP, "rsc_op");
			
			set_xml_property_copy(rsc_op, "id", id);
			set_xml_property_copy(rsc_op, "task", task);
			set_xml_property_copy(rsc_op, "on_node", on_node);
			
			add_node_copy(rsc_op, xml_action->children);
			
			send_ipc_request(crm_ch, options, data,
					 on_node, "lrmd", "tengine",
					 NULL, NULL);
			
			free_xml(options);
			free_xml(data);
			return TRUE;
#endif			
		} else {
			// error
			cl_log(LOG_ERR,
			       "Failed on unsupported command: %s (id=%s) on %s",
			       task, id, on_node);

			return FALSE;
		}
	}
	
	return FALSE;
}

gboolean
process_te_message(xmlNodePtr msg, IPC_Channel *sender)
{
	const char *op = get_xml_attr (msg, XML_TAG_OPTIONS,
				       XML_ATTR_OP, FALSE);

	const char *sys_to = xmlGetProp(msg, XML_ATTR_SYSTO);

	cl_log(LOG_DEBUG, "Processing %s message", op);
	
	if(op == NULL){
		// error
	} else if(strcmp(op, "hello") == 0) {
		// ignore

	} else if(sys_to == NULL || strcmp(sys_to, "tengine") != 0) {
		CRM_DEBUG("Bad sys-to %s", sys_to);
		return FALSE;
		
	} else if(strcmp(op, "transition") == 0) {

		CRM_DEBUG("Initializing graph...");
		initialize_graph();

		xmlNodePtr graph = find_xml_node(msg, "transition_graph");
		CRM_DEBUG("Unpacking graph...");
		unpack_graph(graph);
		CRM_DEBUG("Initiating transition...");
		if(initiate_transition() == FALSE) {
			// nothing to be done.. means we're done.
			cl_log(LOG_INFO, "No actions to be taken..."
			       " transition compelte.");
			send_success();		
		}
		CRM_DEBUG("Processing complete...");
		
		
	} else if(strcmp(op, "event") == 0) {
		const char *true_op = get_xml_attr (msg, XML_TAG_OPTIONS,
						    "true_op", TRUE);
		if(true_op == NULL) {
			cl_log(LOG_ERR,
			       "Illegal update, the original operation must be specified");
			send_abort(msg);
			
		} else if(strcmp(true_op, CRM_OPERATION_CREATE) == 0
		   || strcmp(true_op, CRM_OPERATION_DELETE) == 0
		   || strcmp(true_op, CRM_OPERATION_REPLACE) == 0
		   || strcmp(true_op, CRM_OPERATION_WELCOME) == 0
		   || strcmp(true_op, CRM_OPERATION_SHUTDOWN_REQ) == 0
		   || strcmp(true_op, CRM_OPERATION_ERASE) == 0) {

			// these are always unexpected, trigger the PE
			send_abort(msg);
			
		} else if(strcmp(true_op, CRM_OPERATION_UPDATE) == 0) {
			// this may not be un-expected
			if(extract_event(msg) == FALSE){
				send_abort(msg);
			}
			
		} else {
			cl_log(LOG_ERR,
			       "Did not expect copy of action %s", op);
		}
		
	} else if(strcmp(op, "abort") == 0) {
		initialize_graph();

	} else if(strcmp(op, "quit") == 0) {
		cl_log(LOG_WARNING, "Received quit message, terminating");
		exit(0);
	}
	
	return TRUE;
}

void
send_abort(xmlNodePtr msg)
{	
	xmlNodePtr options = create_xml_node(NULL, "options");

	print_state();
	
	CRM_DEBUG("Sending \"abort\" message");
	
	xml_message_debug(msg, "aborting on this msg");
	
	set_xml_property_copy(options, XML_ATTR_OP, "te_abort");
	
	send_ipc_request(crm_ch, options, NULL,
			 NULL, "dc", "tengine",
			 NULL, NULL);
	
	free_xml(options);
}

void
send_success(void)
{	
	xmlNodePtr options = create_xml_node(NULL, "options");

	print_state();

	CRM_DEBUG("Sending \"complete\" message");
	
	set_xml_property_copy(options, XML_ATTR_OP, "te_complete");
	
	send_ipc_request(crm_ch, options, NULL,
			 NULL, "dc", "tengine",
			 NULL, NULL);
	
	free_xml(options);
}

void
print_state(void)
{
	int lpc = 0;
	cl_log(LOG_DEBUG, "#!!#!!# Start Transitioner state");
	if(graph == NULL) {
		cl_log(LOG_DEBUG, "\tEmpty transition graph");
	} else {
		slist_iter(
			action_list, action_list_t, graph, lpc,

			cl_log(LOG_DEBUG,
			       "\tAction set %d: %d of %d actions invoked",
			       lpc, action_list->index,
			       action_list->index_max);
			);
	}
	
	cl_log(LOG_DEBUG, "#!!#!!# End Transitioner state");
}
