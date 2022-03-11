#include <bits/stdc++.h>
#include "file_manager.h"
#include "errors.h"
using namespace std;

int regionMaxNodes;
int pointMaxNodes;
int rootid;
int newnodeid;
vector<int> parentVec;

void insertQuery(FileHandler& fh,FileManager& fm,vector<int>& qpoint,fstream& outfile)
{
    //first check if root points to null..
    if(rootid==-1)
    {
        PageHandler ph = fh.NewPage ();
	    char *data = ph.GetData ();

        int num = 0;                //root identifier
        memcpy(&data[0],&num,sizeof(int));

        num = 0;                    //split dimension
        memcpy(&data[4],&num,sizeof(int));

        num = 2;                    //this is pointNode.
        memcpy(&data[8],&num,4);

        int offset = 12;
        for(int idx=0;idx<qpoint.size();idx++)
        {
            memcpy(&data[offset],&qpoint[idx],sizeof(int));
            offset += 4;
        }

        num = -1;                   //location currently stores if the next element is present or not..
        memcpy(&data[offset],&num,sizeof(int));

        fh.MarkDirty(0);            // Confirm later..
        fh.FlushPages();

        rootid = newnodeid;
        newnodeid++;

        return;
    }

    int pointNode = pQuery(fh,fm,qpoint,outfile,false);   // returns the pointNode where we can insert this point.
    PageHandler ph = fh.PageAt(pointNode);
    char *data = ph.GetData ();
    
    int num,idx,offset;
    idx = 0;
    offset = 12;
    
    while(idx<pointMaxNodes-1)
    {
        memcpy(&num,&data[offset+qpoint.size()*4],4);
        if(num<=-1) break;                                // last location has -1.
        offset += (qpoint.size() + 1)*4;
        idx++;
    }

    // no overflow..
    if(num==-1)
    {
        offset += (qpoint.size() + 1)*4;
        num = pointNode;                        // what is stored in location by default..
        memcpy(&data[offset-4],&num,4);
        
        for(idx=0;idx<qpoint.size();idx++)
        {
            num = qpoint[idx];
            memcpy(&data[offset],&num,4);
            offset += 4;
        }

        num = -1;
        memcpy(&data[offset],&num,4);          // again -1 inserted in the end.

        return ;
    }
    fh.FlushPages();                            // need to confirm this..

    vector<int> tempVec;                        // just to maintain same arguments..
    Reorganization(fh,fm,qpoint,pointNode,true,tempVec);

    parentVec.resize(0);
}

void Reorganization(FileHandler& fh,FileManager& fm,vector<int>& qpoint,int thisNode,bool isPoint,vector<int>& region)
{
    PageHandler ph = fh.PageAt(thisNode);
    char *data = ph.GetData ();

    int split_dim,offset,num;
    memcpy(&split_dim,&data[4],4);                  // Here confirm about the split_dim...

    //Choose split element..
    vector<int> vecMedian;
    offset = 12;
    if(isPoint)
    {
        for(int idx=0;idx<pointMaxNodes;idx++)
        {
            memcpy(&num,&data[offset+4*split_dim],4);
            offset += 4*(qpoint.size()+1);
            vecMedian.push_back(num);
        }
        vecMedian.push_back(qpoint[split_dim]);
    }
    else
    {
        for(int idx = 0;idx<regionMaxNodes;idx++)
        {
            memcpy(&num,&data[offset+4*(1+split_dim)],4);
            offset += 4*(2*qpoint.size()+1);
            vecMedian.push_back(num);
        }
        vecMedian.push_back(region[(1+split_dim)]);
    }

    nth_element(vecMedian.begin(),vecMedian.begin()+vecMedian.size()/2,vecMedian.end());
    int split_element = vecMedian[vecMedian.size()/2];

    PageHandler left;
    int leftId = newnodeid;

    PageHandler right;
    int rightId = newnodeid + 1;
    newnodeid += 2;

    if(isPoint) NodeSplit(left,right,fh,thisNode,isPoint,split_element,qpoint,leftId,rightId);
    else NodeSplit(left,right,fh,thisNode,isPoint,split_element,region,leftId,rightId);

    // ALERT also deal when parentVec is empty..
    int parentNode = parentVec[parentVec.size()-1];
    parentVec.pop_back();

    vector<int> parentMin(qpoint.size());
    vector<int> parentMax(qpoint.size());

    PageHandler pph = fh.PageAt(parentNode);
    char *pdata = pph.GetData ();
    bool poverflow = true;
    offset = 12;

    for(int idx=0;idx<regionMaxNodes;idx++)
    {
        memcpy(&num,&pdata[offset],4);
        if(num==thisNode)
        {
            memcpy(&parentMin[0],&pdata[offset+4],4*(qpoint.size()));
            memcpy(&parentMax[0],&pdata[4+4*(qpoint.size())],4*qpoint.size());
            memcpy(&pdata[offset],&leftId,4);
            memcpy(&pdata[offset+4*(1+qpoint.size()+split_dim)],&split_element,4);
            // ALERT : Here remember to delete thisNode id from file memory..
            parentMin[split_dim] = split_element;       // for right node to be inserted ..
        }
        else if(num==-1)
        {
            poverflow = false;
            break;
        }
        offset += 4*(2*qpoint.size()+1);
    }
    
    if(!poverflow)
    {
        memcpy(&pdata[offset],&rightId,4);
        memcpy(&pdata[offset+4],&parentMin[0],4*(qpoint.size()));
        memcpy(&pdata[offset+4*(1+qpoint.size())],&parentMax[0],4*(qpoint.size()));
        return ;
    }

    vector<int> newRegion;
    newRegion.push_back(rightId);
    for(int idx=0;idx<qpoint.size();idx++) newRegion.push_back(parentMin[idx]);
    for(int idx=0;idx<qpoint.size();idx++) newRegion.push_back(parentMax[idx]);

    Reorganization(fh,fm,qpoint,parentNode,false,newRegion);
    
    // now need to integrate left,right and delete pointNode..

}

void NodeSplit(PageHandler& left,PageHandler& right,FileHandler& fh,int pointNode,bool isPoint,int split_element,vector<int>& qpoint,int leftId,int rightId)
{
    PageHandler ph = fh.PageAt(pointNode);
    char *data = ph.GetData();
    char *Ldata = left.GetData();
    char *Rdata = right.GetData();

    int offset,num,split_dim,Loffset,Roffset;
    memcpy(&split_dim,&data[4],4);

    memcpy(&Ldata[0],&leftId,4);
    memcpy(&Ldata[4],&split_dim,4);

    memcpy(&Rdata[0],&rightId,4);
    memcpy(&Rdata[4],&split_dim,4);
    
    offset = 12;
    Loffset = 12;
    Roffset = 12;

    if(isPoint)
    {
        num = 2;
        memcpy(&Ldata[Loffset-4],&num,4);
        memcpy(&Rdata[Roffset-4],&num,4);

        for(int idx=0;idx<pointMaxNodes;idx++)
        {
            memcpy(&num,&data[offset+4*split_dim],4);
            if(num<split_element)
            {
                memcpy(&Ldata[Loffset],&data[offset],4*(qpoint.size()+1));
                Loffset += 4*(qpoint.size()+1);
            }
            else
            {
                memcpy(&Rdata[Roffset],&data[offset],4*(qpoint.size()+1));
                Roffset += 4*(qpoint.size()+1);
            }
            offset += 4*(qpoint.size()+1);
        }

        num = -1;
        if(qpoint[split_dim]<split_element)
        {
            memcpy(&Ldata[Loffset],&qpoint[0],4*(qpoint.size()));
            Loffset += 4*(qpoint.size());
            memcpy(&Ldata[Loffset],&num,4);
            Loffset += 4;
        }
        else
        {
            memcpy(&Rdata[Roffset],&qpoint[0],4*(qpoint.size()));
            Roffset += 4*(qpoint.size());
            memcpy(&Rdata[Roffset],&num,4);
            Roffset += 4;    
        }
       
        memcpy(&Ldata[Loffset-4],&num,4);       // to ensure last one is -1..
        memcpy(&Rdata[Roffset-4],&num,4);

        return;                                 // since everything has been completed..
    }

    //if this is region node.
    // here qpoint will be the region node...region node will be of size 2*dim + 1
    num = 1;
    memcpy(&Ldata[Loffset-4],&num,4);
    memcpy(&Rdata[Roffset-4],&num,4);
    int leftnum,rightnum;
    int dim = qpoint.size()/2;

    for(int idx=0;idx<regionMaxNodes;idx++)
    {
        memcpy(&leftnum,&data[offset+4+4*split_dim],4);
        memcpy(&rightnum,&data[offset+4+4*dim+4*split_dim],4);
        // Continue..
    }
    
}

//ALERT write a sorting function for region node...
int pQuery(FileHandler& fh,FileManager& fm,vector<int>& qpoint,fstream& outfile,bool actualP)
{
    int curr_node = rootid;
    int parent_node = rootid;
    int regionsTouched = 0;

    while(true)
    {
        parent_node = curr_node;
        parentVec.push_back(parent_node);

        PageHandler ph = fh.PageAt(curr_node);
        char *data = ph.GetData ();

        int split_dim,child,offset,node_num,valMIN,valMAX,Ntype;
        memcpy(&Ntype,&data[8],4);

        if(Ntype==2) break;                             //Ntype=1 for region and 2 for point node.

        memcpy(&split_dim, &data[4], 4);
        node_num = 0;
        offset = 12;

        while(node_num<regionMaxNodes)
        {
            memcpy(&child,&data[offset],4);
            if(child==-1) break;

            memcpy(&valMIN,&data[offset+4*(1+split_dim)],4);
            memcpy(&valMAX,&data[offset+4*(1+qpoint.size()+split_dim)],4);
            memcpy(&curr_node,&data[offset],4);         // curr_node cannot be lesser than this value..

            if(qpoint[split_dim]<valMAX && qpoint[split_dim]>=valMIN) break;
            //escape the loop if the curr_node is correct...

            offset += 4*(1+2*qpoint.size());
            node_num++;
        }
        // ALERT handle the case when app.. range is not found..
        fh.UnpinPage(parent_node);              // ALERT confirm this..
        regionsTouched++;
    }

    if(!actualP) return curr_node;              // Is it real pQuery... if not return..

    parentVec.resize(0);
    PageHandler ph = fh.PageAt(curr_node);
    char *data = ph.GetData ();
    bool test = false;
    int offset = 12;
    int num_nodes = 0;
    int temp,idx;

    while(num_nodes<pointMaxNodes)
    {
        for(idx=0;idx<qpoint.size();idx++)
        {
            memcpy(&temp,&data[offset+idx*4],sizeof(int));
            if(temp!=qpoint[idx]) break;
        }
        if(idx==qpoint.size()) {test = true;break;}

        offset += (qpoint.size()+1)*4;
        num_nodes++;
    }

    // now  based on test act accordingly..
    //correct this.. later..
    return test;
}

vector<vector<int>> rQuery(FileHandler& fh,FileManager& fm,vector<int>& qpoint,fstream& outfile)
{
    // note here qpoint contains 2*dim...
    vector<vector<int>> pointsList;
    if(rootid==-1) return pointsList;
    int regTouched = 0;
    //for pointTouched use the size of pointsList..

    queue<int> toExplore;
    toExplore.push(rootid);

    while(toExplore.size()>0)
    {
        int curr_node = toExplore.front();
        toExplore.pop();

        PageHandler ph = fh.PageAt(curr_node);
	    char *data = ph.GetData ();

        int offset,num,idx,Ntype;
        memcpy(&Ntype,&data[8],4);
        offset = 12;
        idx = 0;

        // the node is a point node..
        if(Ntype==2)
        {
            vector<int> thisPoint;
            while(true)
            {
                memcpy(&num,&data[offset+4*idx],4);
                thisPoint.push_back(num);
                idx++;

                if(idx==qpoint.size()/2)            //qpoint.size() =  2*dim
                {
                    pointsList.push_back(thisPoint);
                    thisPoint.resize(0);

                    offset += 4*(idx+1);
                    idx = 0;

                    memcpy(&num,&data[offset-4],4);
                    if(num==-1) break;             //the last one will be having -1 
                }
            }
            continue;                   // no need to move forward..
        }

        while(idx<regionMaxNodes)
        {
            memcpy(&num,&data[offset],4);
            if(num==-1) break;
            //check if range overlaps.. then insert in the queue..
            //ask how to check what it means to overlap..
            idx++;
        }        
    }

}

int main(int argc, char* argv[])
{
    assert(argc>3);
    string input_file = argv[1];
    int dim = std::stoi(argv[2]);
    string output_file = argv[3];

    ifstream infile(input_file,ios::in);
    fstream outfile(output_file,ios::out);

    FileManager fm;
    FileHandler fh = fm.CreateFile("temp.txt");
    cout << "File created " << endl;

    //4 for split_dim, 4 for nodetype..
    regionMaxNodes = (PAGE_CONTENT_SIZE - 4 - 4)/((2*dim+1)*4);
    pointMaxNodes = (PAGE_CONTENT_SIZE - 4 - 4)/((dim+1)*4);
    rootid = -1;

    string line;
    while(getline(infile,line))
    {
        string word = "";
        string type = "";
        vector<int> qpoint;
        
        for(auto x:line)
        {
            if(x==' ')
            {
                if(type=="") type = word;
                else qpoint.push_back(stoi(word));
                word = "";
            }
            else word += x;
        }

        if((qpoint.size()!=dim && type!="RQUERY") && (qpoint.size()!=2*dim && type=="RQUERY")){cout<<"ERROR IN INPUT FILE\n"; break;}
        else if(type=="INSERT") insertQuery(fh,fm,qpoint,outfile);
        else if(type=="PQUERY") pQuery(fh,fm,qpoint,outfile,true);
        else if(type=="RQUERY") rQuery(fh,fm,qpoint,outfile);
        else {cout<<"ERROR IN INPUT FILE\n"; break;}
    }
}